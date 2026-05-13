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

local ok, native = pcall(require, "libraries.ghostty_lxl")
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

local shifted_repeat_keys = {
  ["`"] = "~",
  ["1"] = "!",
  ["2"] = "@",
  ["3"] = "#",
  ["4"] = "$",
  ["5"] = "%",
  ["6"] = "^",
  ["7"] = "&",
  ["8"] = "*",
  ["9"] = "(",
  ["0"] = ")",
  ["-"] = "_",
  ["="] = "+",
  ["["] = "{",
  ["]"] = "}",
  ["\\"] = "|",
  [";"] = ":",
  ["'"] = "\"",
  [","] = "<",
  ["."] = ">",
  ["/"] = "?",
}

local named_repeat_keys = {
  space = " ",
  minus = "-",
  equals = "=",
  ["left bracket"] = "[",
  ["right bracket"] = "]",
  backslash = "\\",
  semicolon = ";",
  quote = "'",
  comma = ",",
  period = ".",
  slash = "/",
  grave = "`",
}

local pressed_repeat_keys = {}

local function printable_text_for_key(key, mods)
  if not key or key == "" then return nil end
  mods = mods or {}

  local text = named_repeat_keys[key] or (#key == 1 and key)
  if not text then return nil end

  if mods.shift then
    if text:match("^%l$") then
      text = text:upper()
    else
      text = shifted_repeat_keys[text] or text
    end
  end
  return text
end

local function repeat_text_for_key(key, mods)
  mods = mods or {}
  if mods.ctrl or mods.alt or mods.option or mods.altgr or mods.cmd then return nil end
  return printable_text_for_key(key, mods)
end

local function modifiers_from_keymap(modifiers)
  local source = modifiers or keymap.modkeys or {}
  return {
    ctrl = source.ctrl,
    shift = source.shift,
    alt = source.alt or source.option,
    option = source.option,
    altgr = source.altgr,
    cmd = source.cmd,
  }
end

local function key_combo(key, mods)
  local parts = {}
  if mods.ctrl then parts[#parts + 1] = "ctrl" end
  if mods.shift then parts[#parts + 1] = "shift" end
  if mods.alt then parts[#parts + 1] = "alt" end
  if mods.cmd then parts[#parts + 1] = "cmd" end
  parts[#parts + 1] = key
  return table.concat(parts, "+")
end

local reserved_key_combos = {
  ["alt+t"] = true,
  ["ctrl+shift+`"] = true,
  ["ctrl+shift+c"] = PLATFORM ~= "Mac OS X",
  ["ctrl+shift+v"] = true,
  ["ctrl+shift+w"] = true,
  ["cmd+c"] = PLATFORM == "Mac OS X",
  ["cmd+v"] = PLATFORM == "Mac OS X",
}

local function should_forward_key_to_terminal(key, mods)
  if not key or key == "" then return false end
  mods = mods or {}
  if reserved_key_combos[key_combo(key, mods)] then return false end
  if mods.cmd then return false end
  if mods.ctrl or mods.alt or mods.option or mods.altgr then return true end
  if mods.shift and not printable_text_for_key(key, {}) then return true end
  return false
end

local function clamp_cell(view, col, row)
  local cols = (view.snapshot and view.snapshot.cols) or view.cols or 1
  local rows = (view.snapshot and view.snapshot.rows) or view.rows or 1
  return math.max(1, math.min(cols, col)), math.max(1, math.min(rows, row))
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
  self.osc52_allowed = self.options.osc52 == "allow"

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

function TerminalView:set_target_size(axis, value)
  if axis ~= "x" and axis ~= "y" then return false end
  self.size[axis] = math.max(0, value)
  return true
end

function TerminalView:close()
  if self.terminal then
    self.terminal:close()
    events.emit("terminal-closed", { view = self, terminal = self.terminal })
    self.terminal = nil
  end
  if core.ghostty_view == self then
    core.ghostty_view = nil
    core.ghostty_view_node = nil
    core.ghostty_view_closed = nil
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
  elseif event.kind == "clipboard-write-request" then
    local policy = view.options.osc52
    if policy == "deny" then
      events.emit("clipboard-write-denied", { view = view, terminal = view.terminal, bytes = event.bytes })
      return
    end
    local accept = function()
      system.set_clipboard(event.text or "")
      view.osc52_allowed = true
      events.emit("clipboard-write-accepted", event)
    end
    if policy == "allow" or view.osc52_allowed then
      accept()
    else
      core.command_view:enter("Allow terminal clipboard write? Type yes", {
        submit = function(text)
          if text == "yes" or text == "y" then
            accept()
          else
            events.emit("clipboard-write-denied", { view = view, terminal = view.terminal, bytes = event.bytes })
          end
        end
      })
    end
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

local function draw_cursor(view)
  local cursor = view.snapshot and view.snapshot.cursor
  if not cursor or not cursor.visible then return end

  local x = view.position.x + ((cursor.x or 0) * view.cell_width)
  local y = view.position.y + ((cursor.y or 0) * view.cell_height)
  renderer.draw_rect(
    x,
    y,
    view.cell_width,
    view.cell_height,
    color_or_default(view.options.cursor, style.caret)
  )
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
    local spans = {}
    for _, span in ipairs(row.spans or {}) do
      local x = self.position.x + ((span.x or 0) * self.cell_width)
      if span.bg then
        renderer.draw_rect(x, y, font:get_width(span.text), self.cell_height, span.bg)
      end
      spans[#spans + 1] = { span = span, x = x }
      line[#line + 1] = span.text
    end
    local sel_start, sel_end = selection.row_range(self.selection, row_index, self.snapshot.cols or self.cols or 1)
    if sel_start and sel_end then
      renderer.draw_rect(
        self.position.x + ((sel_start - 1) * self.cell_width),
        y,
        ((sel_end - sel_start) + 1) * self.cell_width,
        self.cell_height,
        style.selection
      )
    end
    for _, item in ipairs(spans) do
      local span = item.span
      renderer.draw_text(font, span.text, item.x, y, span.fg or self.options.foreground or style.text)
    end
    self.visible_rows[row_index] = table.concat(line)
    y = y + self.cell_height
  end
  draw_cursor(self)
end

function TerminalView:on_text_input(text)
  if self.terminal and text and text ~= "" then self.terminal:input_text(text) end
end

function TerminalView:on_key_repeated(key)
  local text = repeat_text_for_key(key, keymap.modkeys)
  if text then
    self.terminal:input_text(text)
    return true
  end
  return false
end

function TerminalView:on_key_pressed(key, scancode, repeated, modifiers)
  if not self.terminal then return false end
  local parts = {}
  modifiers = modifiers_from_keymap(modifiers)
  if modifiers.ctrl then parts[#parts + 1] = "ctrl" end
  if modifiers.shift then parts[#parts + 1] = "shift" end
  if modifiers.alt then parts[#parts + 1] = "alt" end
  parts[#parts + 1] = key
  if self.terminal:send_key {
      key = key,
      mods = modifiers,
      text = printable_text_for_key(key, modifiers),
      repeated = repeated,
      scancode = scancode,
    } then
    return true
  end
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
  return clamp_cell(self, col, row)
end

function TerminalView:copy_selection()
  if not self.terminal or not selection.has_selection(self.selection) then return false end
  local first, last = selection.range(self.selection)
  local text = self.terminal:copy_selection(first.col, first.row, last.col, last.row)
  if not text or text == "" then return false end
  system.set_clipboard(text)
  return true
end

function TerminalView:on_mouse_pressed(button, x, y, clicks)
  if button ~= "left" then return false end
  local col, row = self:convert_coordinates(x, y)
  local mods = keymap.modkeys
  if mods and mods[config.plugins.ghostty.click_modifier] then
    local uri = self.terminal and self.terminal:hyperlink_at(col, row)
    local detected = uri and { kind = uri:match("^https?://") and "url" or "file_url", target = uri, raw = uri }
        or click_to_open.detect(self.visible_rows[row], col)
    if click_to_open.open(detected, self.cwd) then
      events.emit("link-opened", { view = self, terminal = self.terminal, target = detected.target, target_type = detected.kind, line = detected.line, col = detected.col })
      return true
    end
  end
  if self.terminal and self.terminal:mouse_tracking() and not (mods and mods.shift) then
    return self.terminal:send_mouse { action = "press", button = button, x = x - self.position.x, y = y - self.position.y, mods = keymap.modkeys }
  end
  selection.start(self.selection, col, row)
  core.redraw = true
  return true
end

function TerminalView:on_mouse_moved(x, y)
  local col, row = self:convert_coordinates(x, y)
  if self.selection.active then
    if selection.update(self.selection, col, row) then core.redraw = true end
    return true
  end
  if self.terminal and self.terminal:mouse_tracking() then
    self.terminal:send_mouse { action = "motion", x = x - self.position.x, y = y - self.position.y, mods = keymap.modkeys }
  end
end

function TerminalView:on_mouse_released(button, x, y)
  if self.selection.active then
    if button == "left" then
      selection.finish(self.selection)
      core.redraw = true
    end
    return true
  end
  if self.terminal and self.terminal:mouse_tracking() then
    self.terminal:send_mouse { action = "release", button = button, x = x - self.position.x, y = y - self.position.y, mods = keymap.modkeys }
  end
end

function TerminalView:on_mouse_wheel(y)
  if not self.terminal then return false end
  if self.terminal:mouse_tracking() then
    self.terminal:send_mouse { action = "press", button = y > 0 and "wheel_up" or "wheel_down", x = 0, y = 0, mods = keymap.modkeys }
    return true
  end
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
    core.ghostty_view:set_target_size("y", core.ghostty_view.options.drawer_height or config.plugins.ghostty.drawer_height)
    core.ghostty_view_node = core.root_view:get_active_node_default():split("down", core.ghostty_view, { y = true }, true)
  elseif core.ghostty_view_closed then
    core.ghostty_view_node:resize("y", core.ghostty_view_closed)
    core.ghostty_view_closed = nil
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
  ["ghostty:copy-or-interrupt"] = function(view)
    if not view:copy_selection() then view:on_key_pressed("c", nil, false, { ctrl = true }) end
  end,
  ["ghostty:paste"] = function(view)
    local text = system.get_clipboard()
    if text and text ~= "" then view.terminal:paste(text) end
  end,
  ["ghostty:copy-selection"] = function(view)
    view:copy_selection()
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

if PLATFORM == "Mac OS X" then
  keymap.add {
    ["cmd+c"] = "ghostty:copy-or-interrupt",
    ["cmd+v"] = "ghostty:paste",
  }
else
  keymap.add {
    ["ctrl+shift+c"] = "ghostty:copy-selection",
  }
end

keymap.add_direct {
  ["ctrl+c"] = "ghostty:key-ctrl-c",
}

if not keymap.ghostty_repeat_on_key_pressed then
  keymap.ghostty_repeat_on_key_pressed = keymap.on_key_pressed
  function keymap.on_key_pressed(key, scancode, repeated, ...)
    local view = core.active_view
    local mods = modifiers_from_keymap(keymap.modkeys)
    local forwarded = false
    if view and view.on_key_pressed and view.terminal and should_forward_key_to_terminal(key, mods) then
      forwarded = true
      if view:on_key_pressed(key, scancode, repeated, mods) then return true end
    end

    local performed = keymap.ghostty_repeat_on_key_pressed(key, scancode, repeated, ...)
    if performed then return true end

    if view and view.on_key_repeated and view.terminal and repeat_text_for_key(key, keymap.modkeys) then
      if repeated == true or pressed_repeat_keys[key] then
        pressed_repeat_keys[key] = true
        return view:on_key_repeated(key)
      end
      pressed_repeat_keys[key] = true
    end
    if not forwarded and view and view.on_key_pressed and view.terminal and should_forward_key_to_terminal(key, mods) then
      return view:on_key_pressed(key, scancode, repeated, mods)
    end
    return false
  end

  keymap.ghostty_repeat_on_key_released = keymap.on_key_released
  function keymap.on_key_released(key, ...)
    pressed_repeat_keys[key] = nil
    return keymap.ghostty_repeat_on_key_released(key, ...)
  end
end

return {
  TerminalView = TerminalView,
  new_terminal = function(options) return TerminalView(options or {}) end,
  open_tab = open_tab,
  open_drawer = open_drawer,
  on = events.on,
  off = events.off,
  emit = events.emit,
}
