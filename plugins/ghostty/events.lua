local system = require "system"

local events = {}
local handlers = {}

function events.on(name, fn)
  assert(type(name) == "string", "event name must be a string")
  assert(type(fn) == "function", "event handler must be a function")
  local list = handlers[name]
  if not list then
    list = {}
    handlers[name] = list
  end
  list[#list + 1] = fn
  return function()
    events.off(name, fn)
  end
end

function events.off(name, fn)
  local list = handlers[name]
  if not list then return end
  for i = #list, 1, -1 do
    if list[i] == fn then table.remove(list, i) end
  end
end

function events.emit(name, payload)
  payload = payload or {}
  payload.kind = payload.kind or name
  payload.time = payload.time or system.get_time()
  local list = handlers[name]
  if not list then return end
  local copy = { table.unpack(list) }
  for _, fn in ipairs(copy) do
    fn(payload)
  end
end

return events
