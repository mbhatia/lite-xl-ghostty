local keymap = {}

local special = {
  ["return"] = "\r",
  ["keypad enter"] = "\r",
  ["backspace"] = "\x7f",
  ["delete"] = "\x1b[3~",
  ["tab"] = "\t",
  ["shift+tab"] = "\x1b[Z",
  ["escape"] = "\x1b",
  ["up"] = "\x1b[A",
  ["down"] = "\x1b[B",
  ["right"] = "\x1b[C",
  ["left"] = "\x1b[D",
  ["home"] = "\x1b[H",
  ["end"] = "\x1b[F",
  ["pageup"] = "\x1b[5~",
  ["pagedown"] = "\x1b[6~",
  ["insert"] = "\x1b[2~",
  ["f1"] = "\x1bOP",
  ["f2"] = "\x1bOQ",
  ["f3"] = "\x1bOR",
  ["f4"] = "\x1bOS",
  ["f5"] = "\x1b[15~",
  ["f6"] = "\x1b[17~",
  ["f7"] = "\x1b[18~",
  ["f8"] = "\x1b[19~",
  ["f9"] = "\x1b[20~",
  ["f10"] = "\x1b[21~",
  ["f11"] = "\x1b[23~",
  ["f12"] = "\x1b[24~",
}

local app_cursor = {
  ["up"] = "\x1bOA",
  ["down"] = "\x1bOB",
  ["right"] = "\x1bOC",
  ["left"] = "\x1bOD",
  ["home"] = "\x1bOH",
  ["end"] = "\x1bOF",
}

local function ctrl_letter(combo)
  local key = combo:match("^ctrl%+([a-z])$")
  if not key then return nil end
  return string.char(key:byte() - string.byte("a") + 1)
end

local ctrl_special = {
  ["ctrl+space"] = "\x00",
  ["ctrl+["] = "\x1b",
  ["ctrl+\\"] = "\x1c",
  ["ctrl+]"] = "\x1d",
  ["ctrl+^"] = "\x1e",
  ["ctrl+_"] = "\x1f",
}

function keymap.fallback(combo, state)
  state = state or {}
  if state.cursor_application and app_cursor[combo] then
    return app_cursor[combo]
  end
  return special[combo] or ctrl_special[combo] or ctrl_letter(combo)
end

return keymap
