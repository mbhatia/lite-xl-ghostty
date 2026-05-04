local selection = {}

function selection.new()
  return { active = false, anchor = nil, cursor = nil, dragged = false }
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
  state.dragged = false
end

function selection.update(state, col, row)
  if not state.active then return false end
  if state.cursor and state.cursor.col == col and state.cursor.row == row then return false end
  state.cursor = { col = col, row = row }
  state.dragged = true
  return true
end

function selection.finish(state)
  state.active = false
  if not state.dragged then
    state.anchor = nil
    state.cursor = nil
  end
end

function selection.range(state)
  if not state.dragged and not state.active then return nil end
  return normalize(state.anchor, state.cursor)
end

function selection.has_selection(state)
  return selection.range(state) ~= nil
end

function selection.row_range(state, row, cols)
  local first, last = selection.range(state)
  if not first or not last or row < first.row or row > last.row then return nil end
  local start_col = row == first.row and first.col or 1
  local end_col = row == last.row and last.col or cols
  if start_col > end_col then start_col, end_col = end_col, start_col end
  start_col = math.max(1, math.min(cols, start_col))
  end_col = math.max(1, math.min(cols, end_col))
  if end_col < 1 or start_col > cols then return nil end
  return start_col, end_col
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
