local selection = {}

function selection.new()
  return { active = false, anchor = nil, cursor = nil }
end

local function normalize(a, b)
  if not a or not b then return nil end
  if a.row > b.row or (a.row == b.row and a.col > b.col) then
    a, b = b, a
  end
  return a, b
end

function selection.start(state, col, row)
  state.active = true
  state.anchor = { col = col, row = row }
  state.cursor = { col = col, row = row }
end

function selection.update(state, col, row)
  if not state.active then return end
  state.cursor = { col = col, row = row }
end

function selection.finish(state)
  state.active = false
end

function selection.range(state)
  return normalize(state.anchor, state.cursor)
end

function selection.extract(state, rows)
  local first, last = selection.range(state)
  if not first or not last then return "" end
  local parts = {}
  for row = first.row, last.row do
    local text = rows[row] or ""
    local s = row == first.row and first.col or 1
    local e = row == last.row and last.col or #text
    parts[#parts + 1] = text:sub(s, e)
  end
  return table.concat(parts, "\n")
end

return selection
