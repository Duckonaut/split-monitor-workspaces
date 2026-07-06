--- helpers.lua
--- Utility functions shared across the split-monitor-workspaces modules.

-- local globals = require("globals")

local current_path = (...):match("(.-)[^%.]+$") or ""
local globals      = require(current_path .. "globals")

local helpers = {}

--- ============================================================
--- Notification helper
--- ============================================================

--- Show a notification with the given message, prefixed by the plugin name.
--- Only shows notifications if cfg.enable_notifications is true.
---@param msg string
function helpers.notify(msg)
	if globals.cfg.enable_notifications then
		hl.notification.create({ text = "[split-monitor-workspaces] " .. msg, duration = 5000, icon = "info" })
	end
end

--- ============================================================
--- Direction parsing
--- ============================================================

--- Convert "next" / "prev" / "+N" / "-N" / "N" to a signed integer delta.
--- Returns 0 for unrecognised input.
---@param value string
---@return integer
function helpers.direction_to_delta(value)
	if value == "next" then return 1 end
	if value == "prev" then return -1 end
	---@type number|nil
	local n = tonumber(value)
	return (n and math.floor(n)) or 0
end

--- ============================================================
--- Monitor helpers
--- ============================================================

--- Return the effective workspace count for the named monitor,
--- respecting per-monitor overrides from the config.
---@param monitor_name string
---@return integer
function helpers.get_monitor_max_ws(monitor_name)
	---@type SMW.PriorityEntry|nil
	local ov = globals.monitor_max_ws_override[monitor_name]
	return ov and ov.value or globals.cfg.workspace_count or 10
end

--- Return the currently focused monitor, falling back to the monitor under the cursor.
---@return HL.Monitor|nil
function helpers.get_current_monitor()
	return hl.get_active_monitor() or hl.get_monitor_at_cursor()
end

--- Return the primary monitor to focus on startup/remap.
--- Respects cfg.default_monitor; falls back to the monitor with the lowest ID.
--- May return nil when no monitors are connected (e.g. during suspend/wake).
---@return HL.Monitor|nil
function helpers.get_primary_monitor()
	---@type HL.Monitor[]
	local monitors = hl.get_monitors()
	if #monitors == 0 then
		error("[split-monitor-workspaces] No monitors detected?")
		return nil
	end
	---@type string
	local default_monitor = hl.get_config("cursor.default_monitor")
	if default_monitor ~= "" then
		for _, m in ipairs(monitors) do
			if m.name == default_monitor then return m end
		end
	end

	--- fallback when match fails: choose the monitor with the lowest ID
	---@type HL.Monitor
	local primary = monitors[1]
	for _, m in ipairs(monitors) do
		if m.id < primary.id then primary = m end
	end
	return primary
end

--- ============================================================
--- Priority / base-index calculation
--
--- The base index for a monitor is the sum of max_workspaces for
--- all monitors with a strictly lower priority value.
--- ============================================================

---@param monitor_name string
---@return integer
function helpers.calc_base_index(monitor_name)
	---@type SMW.PriorityEntry|nil
	local prio = globals.monitor_priorities[monitor_name]
	if not prio then return 0 end

	---@type integer
	local offset = 0
	for name, p in pairs(globals.monitor_priorities) do
		if p.value < prio.value then
			offset = offset + helpers.get_monitor_max_ws(name)
		end
	end
	return offset
end

--- ============================================================
--- Monitor identifier resolution
---
--- Accepts either a plain port name ("DP-2") or a description with
--- the "desc:" prefix ("desc:ViewSonic Corporation VX2758A-QHD").
--- Description matching is a prefix match, so make+model is enough
--- for the match to succeed and the trailing serial can be omitted.
--- ============================================================
---@param identifier string
---@return string|nil
function helpers.resolve_monitor_identifier(identifier)
    --- extract the part after "desc:" or nil if there's no prefix
	---@type string|nil
	local desc_prefix = identifier:match("^desc:(.*)$")

    --- plain port name, return as-is
	if not desc_prefix then
		return identifier
	end

    -- trim leading and trailing spaces just in case
    desc_prefix = desc_prefix:gsub("^%s+", ""):gsub("%s+$", "")
	if desc_prefix == "" then
		return nil
	end

    --- match the prefix against each monitor's description
	for _, monitor in ipairs(hl.get_monitors()) do
		---@type string
		local desc = monitor.description or ""
		if desc:sub(1, #desc_prefix) == desc_prefix then
			return monitor.name
		end
	end

    --- no monitor matches the description
	return nil
end

--- Registers priorities and workspace-count overrides for all currently available monitors
function helpers.load_config_for_all_monitors()
    for _, monitor in ipairs(hl.get_monitors()) do
        helpers.load_config_for_monitor(monitor)
    end
end

--- Registers priorities and workspace-count overrides only for the passed monitor
---@param monitor HL.Monitor
function helpers.load_config_for_monitor(monitor)
    for i, name in ipairs(globals.cfg.monitor_priority) do
        if helpers.resolve_monitor_identifier(name) == monitor.name then
            globals.monitor_priorities[monitor.name] = { value = i - 1, from_config = true }
        end
    end
    for name, count in pairs(globals.cfg.max_workspaces) do
        if helpers.resolve_monitor_identifier(name) == monitor.name then
            globals.monitor_max_ws_override[monitor.name] = { value = count, from_config = true }
        end
    end
end

--- ============================================================
--- Workspace name resolution
--
--- Given a monitor and a user-facing workspace string, returns the
--- absolute Hyprland workspace name assigned to that monitor.
--
--- Supported formats:
---   "empty"   -> first empty workspace on this monitor, or last if all occupied
---   "+N"/"-N" -> relative offset from the currently active workspace
---   "N"       -> 1-based index within the monitor's assigned range
---   other     -> treated as a named workspace and returned unchanged
--- ============================================================

---@param monitor HL.Monitor
---@param workspace_str string
---@return string
function helpers.get_workspace_from_monitor(monitor, workspace_str)
	---@type string[]|nil
	local ws_list = globals.monitor_workspace_map[monitor.id]
	if not ws_list or #ws_list == 0 then return workspace_str end

	if workspace_str == "empty" then
		for _, name in ipairs(ws_list) do
			---@type HL.Workspace|nil
			local ws = hl.get_workspace(name)
			if ws == nil or ws.is_empty then return name end
		end
		return ws_list[#ws_list]
	end

	---@type string
	local first_char = workspace_str:sub(1, 1)
	---@type integer|nil
	local idx

	if first_char == "+" or first_char == "-" then
		--- relative offset from the current workspace
		---@type integer
		local delta = helpers.direction_to_delta(workspace_str)
		if delta == 0 then return workspace_str end

		---@type string|nil
		local active_name = monitor.active_workspace and monitor.active_workspace.name
		---@type integer|nil
		local cur = nil
		for i, name in ipairs(ws_list) do
			if name == active_name then
				cur = i; break
			end
		end
		if not cur then return workspace_str end
		idx = cur + delta
	else
		---@type number|nil
		local n = tonumber(workspace_str)
		if not n then
			--- unrecognised -> treat as a named workspace passthrough
			return workspace_str
		end
		idx = math.floor(n) --- 1-based index into the monitor's workspace list
	end

	if idx < 1 then
		return globals.cfg.enable_wrapping and ws_list[#ws_list] or ws_list[1]
	elseif idx > #ws_list then
		return globals.cfg.enable_wrapping and ws_list[1] or ws_list[#ws_list]
	end
	return ws_list[idx]
end

return helpers
