-- mod-version:3
local core = require "core"
local command = require "core.command"
local config = require "core.config"
local keymap = require "core.keymap"
local style = require "core.style"
local common = require "core.common"
local View = require "core.view"
local renderer = require "renderer"
local system = require "system"

local defaults = require "plugins.ghostty.config"
local events = require "plugins.ghostty.events"
local terminal_keys = require "plugins.ghostty.keymap"
local selection = require "plugins.ghostty.selection"
local click_to_open = require "plugins.ghostty.click_to_open"
local project = require "plugins.ghostty.project"

local ok, native = pcall(require, "plugins.ghostty.libghostty_lxl")
if not ok then native = nil end

local TerminalView = View:extend()

local function command_from_options(options)
  if options.command then return options.command, options.shell == true end
  return { options.shell_path or config.plugins.ghostty.shell or defaults.shell, "-l" }, false
end

local function cell_size(font)
  local w = font:get_width("M")
  local h = font:get_height()
  return math.max(1, math.floor(w)), math.max(1, math.floor(h))
end

function TerminalView:new(options)
  TerminalView.super.new(self)
  self.options = common.merge(config.plugins.ghostty, options or {})
  self.title = self.options.title or "Ghostty"
  self.cwd = self.options.cwd or project.root()
  self.close_on_exit = (options and options.close_on_exit)
      or (self.options.kind == "agent" and self.options.agent_close_on_exit)
      or self.options.close_on_exit
      or "clean_exit"
  self.selection = selection.new()
  self.snapshot = nil
  self.visible_rows = {}
  self.exited = false
  self.hover = nil
  self.scrollable_size = 0

  if native then
    local command_value, shell = command_from_options(self.options)
    self.terminal = native.new {
      cols = 80,
      rows = 24,
      cell_width = 8,
      cell_height = 16,
      max_scrollback = self.options.max_scrollback,
      command = command_value,
      shell = shell,
      cwd = self.cwd,
      env = common.merge({ TERM = self.options.term }, self.options.env or {}),
      osc_max_bytes = self.options.osc_max_bytes,
    }
    events.emit("terminal-created", { view = self, terminal = self.terminal })
  end
end

function TerminalView:get_name()
  return self.title or "Ghostty"
end

function TerminalView:supports_text_input()
  return true
end

function TerminalView:close()
  if self.terminal then
    self.terminal:close()
    events.emit("terminal-closed", { view = self, terminal = self.terminal })
    self.terminal = nil
  end
end

local function emit_native_event(view, event)
  event.view = view
  event.terminal = view.terminal
  if event.kind == "title-changed" and event.title then
    view.title = event.title
  elseif event.kind == "cwd-changed" and event.cwd then
    event.previous_cwd = view.cwd
    view.cwd = event.cwd
  elseif event.kind == "terminal-exited" then
    view.exited = true
    event.clean = event.code == 0 and event.signal == 0
  elseif event.kind == "notification" then
    core.log("%s", event.title and (event.title .. ": " .. (event.body or "")) or (event.body or ""))
  end
  events.emit(event.kind, event)
end

function TerminalView:update()
  TerminalView.super.update(self)
  if not self.terminal then return end

  local font = self.options.font or style.code_font
  local cw, ch = cell_size(font)
  local cols = math.max(2, math.floor(self.size.x / cw))
  local rows = math.max(1, math.floor(self.size.y / ch))
  if cols ~= self.cols or rows ~= self.rows or cw ~= self.cell_width or ch ~= self.cell_height then
    self.cols, self.rows, self.cell_width, self.cell_height = cols, rows, cw, ch
    self.terminal:resize(cols, rows, cw, ch)
  end

  for _, event in ipairs(self.terminal:poll_events()) do
    emit_native_event(self, event)
  end

  if self.terminal:is_dirty() or not self.snapshot then
    self.snapshot = self.terminal:update_render()
    self.terminal:clear_dirty()
    core.redraw = true
  end

  if self.exited and (self.close_on_exit == "always"
      or (self.close_on_exit == "clean_exit" and self.terminal:exited())) then
    local node = core.root_view.root_node:get_node_for_view(self)
    if node then node:close_view(core.root_view.root_node, self) end
  end
end

local function color_or_default(color, fallback)
  return color or fallback
end

function TerminalView:draw()
  self:draw_background(style.background)
  local font = self.options.font or style.code_font
  local bg = color_or_default(self.options.background, style.background)
  renderer.draw_rect(self.position.x, self.position.y, self.size.x, self.size.y, bg)
  if not self.snapshot or not self.snapshot.rows_data then
    local msg = native and "" or "Native Ghostty module is not built"
    if msg ~= "" then
      renderer.draw_text(font, msg, self.position.x, self.position.y, style.dim)
    end
    return
  end

  self.visible_rows = {}
  local y = self.position.y
  for row_index, row in ipairs(self.snapshot.rows_data) do
    local line = {}
    for _, span in ipairs(row.spans or {}) do
      local x = self.position.x + ((span.x or 0) * self.cell_width)
      if span.bg then
        renderer.draw_rect(x, y, font:get_width(span.text), self.cell_height, span.bg)
      end
      renderer.draw_text(font, span.text, x, y, span.fg or self.options.foreground or style.text)
      line[#line + 1] = span.text
    end
    self.visible_rows[row_index] = table.concat(line)
    y = y + self.cell_height
  end
end

function TerminalView:on_text_input(text)
  if self.terminal and text and text ~= "" then self.terminal:input_text(text) end
end

function TerminalView:on_key_pressed(key, scancode, repeated, modifiers)
  if not self.terminal then return false end
  local parts = {}
  modifiers = modifiers or {}
  if modifiers.ctrl then parts[#parts + 1] = "ctrl" end
  if modifiers.shift then parts[#parts + 1] = "shift" end
  if modifiers.alt then parts[#parts + 1] = "alt" end
  parts[#parts + 1] = key
  local encoded = terminal_keys.fallback(table.concat(parts, "+"))
  if encoded then
    self.terminal:write(encoded)
    return true
  end
  return false
end

function TerminalView:convert_coordinates(x, y)
  local col = math.floor((x - self.position.x) / (self.cell_width or 1)) + 1
  local row = math.floor((y - self.position.y) / (self.cell_height or 1)) + 1
  return col, row
end

function TerminalView:on_mouse_pressed(button, x, y, clicks)
  if button ~= "left" then return false end
  local col, row = self:convert_coordinates(x, y)
  local mods = keymap.modkeys
  if mods and mods[config.plugins.ghostty.click_modifier] then
    local detected = click_to_open.detect(self.visible_rows[row], col)
    if click_to_open.open(detected, self.cwd) then
      events.emit("link-opened", { view = self, terminal = self.terminal, target = detected.target, target_type = detected.kind, line = detected.line, col = detected.col })
      return true
    end
  end
  selection.start(self.selection, col, row)
  return true
end

function TerminalView:on_mouse_moved(x, y)
  local col, row = self:convert_coordinates(x, y)
  selection.update(self.selection, col, row)
end

function TerminalView:on_mouse_released(button)
  if button == "left" then selection.finish(self.selection) end
end

function TerminalView:on_mouse_wheel(y)
  if not self.terminal then return false end
  self.terminal:scroll(y > 0 and -3 or 3)
  return true
end

local function open_tab(options)
  local view = TerminalView(options or {})
  core.root_view:get_active_node_default():add_view(view)
  return view
end

local function open_drawer(options)
  if not core.ghostty_view then
    core.ghostty_view = TerminalView(common.merge({ drawer_height = config.plugins.ghostty.drawer_height }, options or {}))
    core.ghostty_view_node = core.root_view:get_active_node_default():split("down", core.ghostty_view, { y = true }, true)
  end
  core.set_active_view(core.ghostty_view)
  return core.ghostty_view
end

local function toggle_drawer()
  if not core.ghostty_view then return open_drawer() end
  if core.ghostty_view_closed then
    core.ghostty_view_node:resize("y", core.ghostty_view_closed)
    core.ghostty_view_closed = nil
  else
    core.ghostty_view_closed = core.ghostty_view.size.y
    core.ghostty_view_node:resize("y", 0)
  end
  return core.ghostty_view
end

command.add(nil, {
  ["ghostty:toggle-drawer"] = toggle_drawer,
  ["ghostty:open-drawer"] = open_drawer,
  ["ghostty:open-tab"] = open_tab,
  ["ghostty:spawn-agent"] = function(text)
    local spawn = function(command_text)
      return open_tab { kind = "agent", title = command_text, command = command_text, shell = true, close_on_exit = config.plugins.ghostty.agent_close_on_exit }
    end
    if text then return spawn(text) end
    core.command_view:enter("Agent Command", { submit = spawn })
  end,
})

command.add(function()
  return core.active_view and core.active_view:is(TerminalView), core.active_view
end, {
  ["ghostty:close-terminal"] = function(view) view:close() end,
  ["ghostty:key-return"] = function(view) view:on_key_pressed("return") end,
  ["ghostty:key-keypad-enter"] = function(view) view:on_key_pressed("keypad enter") end,
  ["ghostty:key-backspace"] = function(view) view:on_key_pressed("backspace") end,
  ["ghostty:key-delete"] = function(view) view:on_key_pressed("delete") end,
  ["ghostty:key-tab"] = function(view) view:on_key_pressed("tab") end,
  ["ghostty:key-shift-tab"] = function(view) view:on_key_pressed("tab", nil, false, { shift = true }) end,
  ["ghostty:key-escape"] = function(view) view:on_key_pressed("escape") end,
  ["ghostty:key-up"] = function(view) view:on_key_pressed("up") end,
  ["ghostty:key-down"] = function(view) view:on_key_pressed("down") end,
  ["ghostty:key-right"] = function(view) view:on_key_pressed("right") end,
  ["ghostty:key-left"] = function(view) view:on_key_pressed("left") end,
  ["ghostty:key-home"] = function(view) view:on_key_pressed("home") end,
  ["ghostty:key-end"] = function(view) view:on_key_pressed("end") end,
  ["ghostty:key-pageup"] = function(view) view:on_key_pressed("pageup") end,
  ["ghostty:key-pagedown"] = function(view) view:on_key_pressed("pagedown") end,
  ["ghostty:key-ctrl-c"] = function(view) view:on_key_pressed("c", nil, false, { ctrl = true }) end,
  ["ghostty:paste"] = function(view)
    local text = system.get_clipboard()
    if text and text ~= "" then view.terminal:paste(text) end
  end,
  ["ghostty:copy-selection"] = function(view)
    system.set_clipboard(selection.extract(view.selection, view.visible_rows))
  end,
  ["ghostty:scroll-up"] = function(view) if view.terminal then view.terminal:scroll(-3) end end,
  ["ghostty:scroll-down"] = function(view) if view.terminal then view.terminal:scroll(3) end end,
  ["ghostty:clear"] = function(view) if view.terminal then view.terminal:write("\x0c") end end,
  ["ghostty:spawn-command"] = function(view, text)
    if text then view.terminal:write(text .. "\r") end
  end,
})

keymap.add {
  ["alt+t"] = "ghostty:toggle-drawer",
  ["ctrl+shift+`"] = "ghostty:open-tab",
  ["return"] = "ghostty:key-return",
  ["keypad enter"] = "ghostty:key-keypad-enter",
  ["backspace"] = "ghostty:key-backspace",
  ["shift+backspace"] = "ghostty:key-backspace",
  ["delete"] = "ghostty:key-delete",
  ["tab"] = "ghostty:key-tab",
  ["shift+tab"] = "ghostty:key-shift-tab",
  ["escape"] = "ghostty:key-escape",
  ["up"] = "ghostty:key-up",
  ["down"] = "ghostty:key-down",
  ["right"] = "ghostty:key-right",
  ["left"] = "ghostty:key-left",
  ["home"] = "ghostty:key-home",
  ["end"] = "ghostty:key-end",
  ["pageup"] = "ghostty:key-pageup",
  ["pagedown"] = "ghostty:key-pagedown",
  ["ctrl+shift+v"] = "ghostty:paste",
  ["ctrl+shift+w"] = "ghostty:close-terminal",
}

keymap.add_direct {
  ["ctrl+c"] = "ghostty:key-ctrl-c",
}

return {
  TerminalView = TerminalView,
  new_terminal = function(options) return TerminalView(options or {}) end,
  open_tab = open_tab,
  open_drawer = open_drawer,
  on = events.on,
  off = events.off,
  emit = events.emit,
}
