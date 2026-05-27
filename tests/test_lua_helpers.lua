package.path = "./?.lua;./?/init.lua;" .. package.path

local function check(value, message)
  if not value then error(message or "check failed", 2) end
end

local opened_url
local exec_command
local opened_doc
local selection_line
local selection_col

package.preload["system"] = function()
  return {
    get_time = function() return 123 end,
    exec = function(command) exec_command = command end,
  }
end

package.preload["core"] = function()
  return {
    root_project = function() return { path = "/project" } end,
    project_dir = "/lite-project",
    open_doc = function(filename)
      opened_doc = filename
      return {
        set_selection = function(_, line, col)
          selection_line = line
          selection_col = col
        end
      }
    end,
    root_view = {
      open_doc = function(_, doc)
        return doc
      end
    },
    active_view = {
      doc = {
        set_selection = function(_, line, col)
          selection_line = line
          selection_col = col
        end
      }
    }
  }
end

local events = require "plugins.ghostty.events"
local keymap = require "plugins.ghostty.keymap"
local selection = require "plugins.ghostty.selection"
local click = require "plugins.ghostty.click_to_open"
local project = require "plugins.ghostty.project"

local seen
local unsubscribe = events.on("cwd-changed", function(event)
  seen = event
end)
events.emit("cwd-changed", { cwd = "/tmp" })
check(seen.cwd == "/tmp", "event handler should receive payload")
check(seen.time == 123, "event should receive default time")
unsubscribe()
seen = nil
events.emit("cwd-changed", { cwd = "/other" })
check(seen == nil, "unsubscribe should remove handler")

check(keymap.fallback("return") == "\r", "return fallback")
check(keymap.fallback("shift+tab") == "\x1b[Z", "shift-tab fallback")
check(keymap.fallback("ctrl+c") == "\003", "ctrl-letter fallback")
check(keymap.fallback("ctrl+z") == "\026", "ctrl-z fallback")
check(keymap.fallback("ctrl+space") == "\000", "ctrl-space fallback")
check(keymap.fallback("ctrl+[") == "\027", "ctrl-left-bracket fallback")
check(keymap.fallback("ctrl+\\") == "\028", "ctrl-backslash fallback")
check(keymap.fallback("ctrl+]") == "\029", "ctrl-right-bracket fallback")
check(keymap.fallback("up", { cursor_application = true }) == "\x1bOA", "application cursor fallback")

local state = selection.new()
selection.start(state, 2, 1)
selection.update(state, 3, 2)
local first, last = selection.range(state)
check(first.row == 1 and first.col == 2, "selection first coordinate")
check(last.row == 2 and last.col == 3, "selection last coordinate")
check(selection.extract(state, { "abcd", "wxyz" }) == "bcd\nwxy", "selection extract")

local detected = click.detect("see src/main.c:12:4, please", 8)
check(detected.kind == "path", "path kind")
check(detected.target == "src/main.c", "path target")
check(detected.line == 12 and detected.col == 4, "path coordinates")
check(click.resolve_file("src/main.c", "/cwd", "/project") == "/cwd/src/main.c", "cwd resolution")
check(click.resolve_file("src/main.c", nil, "/project") == "/project/src/main.c", "project resolution")
check(project.root() == "/project", "root_project takes precedence when available")

package.loaded.core.root_project = nil
check(project.root() == "/lite-project", "project_dir fallback")

detected = click.detect("visit https://example.com/test).", 10)
check(detected.kind == "url", "url kind")
check(detected.target == "https://example.com/test", "url trims trailing punctuation")
check(click.open(detected, "/cwd"), "url open")
check(exec_command and exec_command:match("https://example%.com/test"), "system exec url open")

package.loaded.system.open_url = function(url) opened_url = url end
check(click.open(detected, "/cwd"), "url open_url open")
check(opened_url == "https://example.com/test", "system open_url takes precedence")

detected = { kind = "path", target = "src/main.c", line = 9, col = 2 }
check(click.open(detected, "/cwd"), "file open")
check(opened_doc == "/cwd/src/main.c", "file resolved against cwd")
check(selection_line == 9 and selection_col == 2, "cursor selection set")

local config_stub = { plugins = { ghostty = {} } }
package.preload["core.config"] = function()
  return config_stub
end
package.preload["core.style"] = function()
  return {
    code_font = {},
    background = {},
    caret = {},
    text = {},
    syntax = { normal = {} },
  }
end
package.preload["core.common"] = function()
  return {
    merge = function(a, b)
      local out = {}
      for k, v in pairs(a or {}) do out[k] = v end
      for k, v in pairs(b or {}) do out[k] = v end
      return out
    end,
    color = function(value) return value end,
  }
end
package.preload["core.command"] = function()
  return { add = function() end }
end
package.preload["core.view"] = function()
  local View = {}
  function View:extend()
    local class = { super = self }
    class.__index = class
    setmetatable(class, { __index = self })
    return class
  end
  function View:new() end
  return View
end
package.preload["core.emptyview"] = function()
  return function() return {} end
end
local core_keymap = {
  modkeys = {},
  map = {},
  on_key_pressed = function() return true end,
  on_key_released = function() return false end,
  add = function() end,
  add_direct = function() end,
}
package.preload["core.keymap"] = function() return core_keymap end
package.preload["renderer"] = function() return {} end

local forwarded
package.loaded.core.active_view = {
  terminal = {},
  on_key_pressed = function(_, key_name, _, _, mods)
    forwarded = { key = key_name, ctrl = mods.ctrl }
    return true
  end,
}
core_keymap.modkeys = { ctrl = true }
require "plugins.ghostty.init"
check(core_keymap.on_key_pressed("c", nil, false), "terminal ctrl-c should be handled")
check(forwarded and forwarded.key == "c" and forwarded.ctrl, "terminal ctrl-c should be forwarded before editor keymap")
