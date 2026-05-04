local core = require "core"
local system = require "system"
local project = require "plugins.ghostty.project"

local click = {}

local trailing = { ["."] = true, [","] = true, [";"] = true, [":"] = true }
local closing = { [")"] = "(", ["]"] = "[", ["}"] = "{" }

local function shell_quote(value)
  return string.format("%q", value)
end

local function open_url(url)
  if type(system.open_url) == "function" then
    system.open_url(url)
    return true
  end
  if type(system.exec) ~= "function" then return false end

  if PLATFORM == "Windows" then
    system.exec("start " .. shell_quote(url))
  elseif PLATFORM == "Linux" then
    system.exec("xdg-open " .. shell_quote(url))
  else
    system.exec("open " .. shell_quote(url))
  end
  return true
end

local function trim_target(text)
  while #text > 0 do
    local last = text:sub(-1)
    if trailing[last] then
      text = text:sub(1, -2)
    elseif closing[last] then
      local open = closing[last]
      local opens = select(2, text:gsub("%" .. open, ""))
      local closes = select(2, text:gsub("%" .. last, ""))
      if closes > opens then text = text:sub(1, -2) else break end
    else
      break
    end
  end
  return text
end

function click.detect(text, col)
  if not text or text == "" then return nil end
  col = col or 1
  local patterns = {
    { "url", "https?://[%w%p]+" },
    { "file_url", "file://[%w%p]+" },
    { "path", "[%w_./~%-]+:%d+:%d+" },
    { "path", "[%w_./~%-]+:%d+" },
    { "path", "/[%w_./%-]+" },
    { "path", "[%w_.%-]+/[%w_./%-]+" },
  }
  for _, entry in ipairs(patterns) do
    local kind, pattern = entry[1], entry[2]
    local start_at = 1
    while true do
      local s, e = text:find(pattern, start_at)
      if not s then break end
      if col >= s and col <= e then
        local raw = trim_target(text:sub(s, e))
        local path, line, column = raw:match("^(.-):(%d+):(%d+)$")
        if path then return { kind = kind, target = path, line = tonumber(line), col = tonumber(column), raw = raw } end
        path, line = raw:match("^(.-):(%d+)$")
        if path and kind == "path" then return { kind = kind, target = path, line = tonumber(line), raw = raw } end
        return { kind = kind, target = raw, raw = raw }
      end
      start_at = e + 1
    end
  end
  return nil
end

function click.resolve_file(target, cwd, project_root)
  if not target or target == "" then return nil end
  if target:match("^file://") then return target:gsub("^file://", "") end
  if target:sub(1, 1) == "/" or target:sub(1, 2) == "~/" then return target end
  if cwd and cwd ~= "" then return cwd .. "/" .. target end
  if project_root and project_root ~= "" then return project_root .. "/" .. target end
  return nil
end

function click.open(detected, cwd)
  if not detected then return false end
  if detected.kind == "url" then
    return open_url(detected.target)
  end
  local root = project.root()
  local filename = click.resolve_file(detected.target, cwd, root)
  if not filename then return false end
  core.root_view:open_doc(core.open_doc(filename))
  local doc = core.active_view and core.active_view.doc
  if doc and detected.line then
    local line = math.max(1, detected.line)
    local col = math.max(1, detected.col or 1)
    doc:set_selection(line, col)
  end
  return true
end

return click
