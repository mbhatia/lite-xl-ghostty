local core = require "core"

local project = {}

function project.root()
  if type(core.root_project) == "function" then
    local root = core.root_project()
    if root and root.path then return root.path end
  end

  if core.project_dir and core.project_dir ~= "" then
    return core.project_dir
  end

  local first = core.project_directories and core.project_directories[1]
  return first and first.name or nil
end

return project
