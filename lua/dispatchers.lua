--- dispatchers.lua
--- Imperative implementations of each split-monitor-workspaces action.
--- These are called at runtime (from keybind callbacks and event handlers).
--
--- Each function in this module has a corresponding public closure factory
--- in split-monitor-workspaces.lua that wraps it for use with hl.bind().

local current_path = (...):match("(.-)[^%.]+$") or ""
local globals      = require(current_path .. "globals")
local helpers      = require(current_path .. "helpers")


local dispatchers = {}

--- ============================================================
--- split-workspace
--- ============================================================

---@param workspace_str string
function dispatchers.do_workspace(workspace_str)
	---@type HL.Monitor|nil
	local current_monitor = helpers.get_current_monitor()
	if not current_monitor then
		error("[split-monitor-workspaces] No current monitor? Cannot switch workspace.")
		return
	end

	if globals.cfg.link_monitors then
		--- Gnome-style: switch all monitors to their corresponding workspace.
		for _, monitor in ipairs(hl.get_monitors()) do
			---@type string
			local target_workspace = helpers.get_workspace_from_monitor(monitor, workspace_str)
			--- create the workspace if it does not yet exist
			if hl.get_workspace(target_workspace) == nil then
				hl.dispatch(hl.dsp.focus({ workspace = target_workspace }))
			end
			if monitor.active_workspace and monitor.active_workspace.name == target_workspace then
				--- already on the correct workspace, no need to switch
				goto continue
			end
			if monitor.id == current_monitor.id then
				--- for the current monitor, just focus the workspace
				hl.dispatch(hl.dsp.focus({ workspace = target_workspace }))
				goto continue
			end
			--- for other monitors, set the monitor's active workspace to the target without focusing it
			monitor:set_workspace(target_workspace)

			::continue::
		end
	else
		---@type string
		local resolved = helpers.get_workspace_from_monitor(current_monitor, workspace_str)
		hl.dispatch(hl.dsp.focus({ workspace = resolved }))
	end
end

--- ============================================================
--- split-cycleworkspaces
--- ============================================================

---- Cycle to the next/previous workspace on the current monitor.
---@param value string "next", "prev", "+N", or "-N"
---@param no_wrap boolean if true, do not wrap around when cycling past the first or last workspace
function dispatchers.do_cycle_workspaces(value, no_wrap)
	---@type integer the delta value to apply to the current workspace index
	local delta = helpers.direction_to_delta(value)
	if delta == 0 then
		error("[split-monitor-workspaces] Invalid value for cycle_workspaces: " .. tostring(value))
		return
	end

	-- Depending on whether link_monitors is enabled, either cycle all monitors or just the current one.
	---@type HL.Monitor[]
	local monitors_to_cycle

	if globals.cfg.link_monitors then
		monitors_to_cycle = hl.get_monitors()
	else
		local m = helpers.get_current_monitor()
		monitors_to_cycle = m and { m } or {}
	end

	---@type HL.Monitor|nil
	local current_monitor = helpers.get_current_monitor()
	if not current_monitor then
		error("[split-monitor-workspaces] No current monitor? Cannot cycle workspaces.")
		return
	end

	-- For each monitor (or just the current one), find the currently active workspace in its assigned workspace list,
	-- then switch to the workspace at index + delta, wrapping if necessary.
	for _, monitor in ipairs(monitors_to_cycle) do
		---@type string[]|nil
		local ws_list = globals.monitor_workspace_map[monitor.id]
		if not ws_list then goto continue end

		-- Figure out which workspace is currently active on this monitor, and its index within the monitor's workspace list.
		---@type string|nil
		local active_name = monitor.active_workspace and monitor.active_workspace.name
		---@type integer|nil
		local idx = nil
		for i, name in ipairs(ws_list) do
			if name == active_name then
				idx = i; break
			end
		end
		if not idx then goto continue end

		-- Apply the delta to get the target workspace index, wrapping if necessary and if no_wrap is not set.
		idx = idx + delta
		if idx < 1 then
			if no_wrap then goto continue end
			idx = #ws_list
		elseif idx > #ws_list then
			if no_wrap then goto continue end
			idx = 1
		end

		-- Focus the target workspace. If link_monitors is enabled, also move it to the correct monitor and follow it to ensure all monitors switch together.
		---@type string
		local target = ws_list[idx]
		-- hl.dispatch(hl.dsp.focus({ workspace = target }))
		-- If the monitor being cycled is the current monitor, focus the workspace normally.
		-- Otherwise use set_workspace to avoid stealing focus from the current monitor.
		if monitor.id == current_monitor.id then
			hl.dispatch(hl.dsp.focus({ workspace = target }))
		else
			monitor:set_workspace({ workspace = target })
		end

		::continue::
	end
end

--- ============================================================
--- split-movetoworkspace / split-movetoworkspacesilent
--- ============================================================

--- Move the active window to the specified workspace, optionally without changing focus.
---@param workspace_str string
---@param silent boolean
function dispatchers.do_move_to_workspace(workspace_str, silent)
	---@type HL.Monitor|nil
	local monitor = helpers.get_current_monitor()
	if not monitor then
		error("[split-monitor-workspaces] No current monitor? Cannot move window to workspace.")
		return
	end

	---@type string
	local resolved = helpers.get_workspace_from_monitor(monitor, workspace_str)

	if hl.plugin.hy3 then
		-- Explicit hy3 support: use the hy3-specific dispatchers instead of Hyprland's.
		if silent then
			hl.dispatch(hl.plugin.hy3.move_to_workspace(resolved))
		else
			hl.dispatch(hl.plugin.hy3.move_to_workspace(resolved, { follow = true }))
		end
	else
		-- No hy3 detected: simply use Hyprland's dispatchers.
		if silent then
			hl.dispatch(hl.dsp.window.move({ workspace = resolved, follow = false }))
		else
			hl.dispatch(hl.dsp.window.move({ workspace = resolved }))
		end
	end

	-- If link_monitors is enabled and we're following the window, call do_workspace to ensure *all* monitors switch to the correct workspaces,
	-- not just the one the window is being moved to.
	if globals.cfg.link_monitors and not silent then
		dispatchers.do_workspace(workspace_str)
	end
end

--- ============================================================
--- split-grabroguewindows
--- ============================================================

function dispatchers.do_grab_rogue_windows()
	---@type HL.Monitor|nil
	local current_monitor = helpers.get_current_monitor()
	if not current_monitor then return end
	---@type HL.Workspace|nil
	local current_ws = current_monitor.active_workspace
	if not current_ws then return end

	--- build a set of all workspace names that belong to any mapped monitor
	---@type table<string, boolean>
	local mapped = {}
	for _, ws_list in pairs(globals.monitor_workspace_map) do
		for _, name in ipairs(ws_list) do
			mapped[name] = true
		end
	end

	---@type HL.Window[]
	local windows = hl.get_windows()
	for _, window in ipairs(windows) do
		if not window.mapped then goto continue end
		if not window.workspace then goto continue end
		if window.workspace.special then goto continue end

		if not mapped[window.workspace.name] then
			print(string.format(
				"[split-monitor-workspaces] Moving rogue window '%s' from workspace %s to %s",
				window.title, window.workspace.name, current_ws.name))
			--- use the address selector to target a specific non-active window
			hl.dispatch(hl.dsp.window.move({ workspace = current_ws.name, window = window, follow = false }))
		end

		::continue::
	end
end

return dispatchers
