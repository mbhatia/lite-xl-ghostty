local config = require "core.config"
local style = require "core.style"
local common = require "core.common"

local default_shell = os.getenv("SHELL") or "/bin/sh"
local defaults = {
  term = "xterm-ghostty",
  shell = default_shell,
  drawer_height = 300,
  font = style.code_font,
  max_scrollback = 10000,
  close_on_exit = "clean_exit",
  agent_close_on_exit = "never",
  inversion_key = "shift",
  mac_option_as_meta = true,
  click_modifier = PLATFORM == "Mac OS X" and "cmd" or "ctrl",
  osc52 = "ask",
  osc52_max_bytes = 1024 * 1024,
  osc_max_bytes = 1024 * 1024,
  paste_warning = true,
  kitty_graphics = false,
  minimum_contrast_ratio = 3,
  debug = false,
  background = style.background,
  foreground = style.syntax and style.syntax.normal or style.text,
  cursor = style.caret,
}

defaults.config_spec = {
  name = "Ghostty Terminal",
  { label = "Terminal Type", path = "term", type = "STRING", default = defaults.term },
  { label = "Terminfo Directory", path = "terminfo", type = "STRING" },
  { label = "Shell", path = "shell", type = "STRING", default = defaults.shell },
  { label = "Terminal Drawer Height", path = "drawer_height", type = "NUMBER", default = defaults.drawer_height },
  { label = "Scrollback Lines", path = "max_scrollback", type = "NUMBER", default = defaults.max_scrollback },
  { label = "OSC 52 Policy", path = "osc52", type = "STRING", default = defaults.osc52 },
  { label = "Paste Warning", path = "paste_warning", type = "TOGGLE", default = defaults.paste_warning },
  { label = "Debug Logging", path = "debug", type = "TOGGLE", default = defaults.debug },
}

config.plugins.ghostty = common.merge(defaults, config.plugins.ghostty)

return config.plugins.ghostty
