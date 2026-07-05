#include "helpers.hpp"
#include "globals.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include <algorithm>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string_view>

const char* translateConfigKey(const char* rawKey)
{
    if (Config::mgr()->type() != Config::CONFIG_LUA || !std::string_view{rawKey}.starts_with("plugin:"))
        return rawKey;

    static std::map<std::string, std::string> luaNames;
    auto [it, inserted] = luaNames.try_emplace(rawKey);
    if (inserted) {
        it->second = it->first;
        std::ranges::replace(it->second, '-', '_');
    }
    return it->second.c_str();
}

void raiseNotification(const std::string& message, float timeout)
{
    if (g_config.enableNotifications->value()) {
        HyprlandAPI::addNotification(PHANDLE, message, s_pluginColor, timeout);
    }
}

bool isHy3Available()
{
    if (g_hy3Status == Hy3Status::DISABLED || g_hy3Status == Hy3Status::NOT_DETECTED)
        return false;

    if (g_hy3Status == Hy3Status::DETECTED)
        return true;

    // detection: check if the hy3 plugin registered its dispatcher
    if (g_pKeybindManager->m_dispatchers.contains("hy3:movetoworkspace")) {
        g_hy3Status = Hy3Status::DETECTED;
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] hy3 plugin detected, using hy3 dispatchers for move operations");
        return true;
    }

    g_hy3Status = Hy3Status::NOT_DETECTED;
    return false;
}

std::string runDispatcher(const std::string& dispatcher, const std::string& args)
{
    if (!g_pKeybindManager) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Keybind manager is not available");
        return "error: keybind manager unavailable";
    }
    const auto it = g_pKeybindManager->m_dispatchers.find(dispatcher);
    if (it == g_pKeybindManager->m_dispatchers.end()) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Dispatcher not found: {}", dispatcher);
        return "error: dispatcher not found: " + dispatcher;
    }
    const auto result = it->second(args);
    return result.success ? "ok" : result.error;
}

std::string dispatchMoveToWorkspace(const std::string& workspaceName, bool silent)
{
    if (isHy3Available()) {
        if (silent) {
            return runDispatcher("hy3:movetoworkspace", workspaceName);
        }
        return runDispatcher("hy3:movetoworkspace", workspaceName + ",follow");
    }
    if (silent) {
        return runDispatcher("movetoworkspacesilent", workspaceName);
    }
    return runDispatcher("movetoworkspace", workspaceName);
}

int directionToDelta(const std::string& direction)
{
    if (direction == "next")
        return 1;
    if (direction == "prev")
        return -1;
    try {
        // this supports -x, +x and x
        return std::stoi(direction);
    }
    catch (const std::invalid_argument&) {
        Log::logger->log(Log::DEBUG, "[split-monitor-workspaces] Invalid direction value: {}", direction.c_str());
    }
    // fallback if input is incorrect
    return 0;
}

int64_t getMonitorMaxWorkspaces(const std::string& name)
{
    // avoid default initialization with []
    return g_vMonitorMaxWorkspaces.contains(name) ? g_vMonitorMaxWorkspaces[name] : g_config.workspaceCount->value();
}

PHLMONITOR getPrimaryMonitor()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Determining primary monitor");

    // The hyprland config can specify a default monitor to focus on startup, the plugin respects that setting
    if (!g_defaultMonitor.empty()) {
        for (const PHLMONITOR& monitor : State::monitorState()->monitors()) {
            if (monitor->m_name == g_defaultMonitor) {
                Log::logger->log(Log::INFO, "[split-monitor-workspaces] Using default monitor '{}' from config", g_defaultMonitor.c_str());
                return monitor;
            }
        }
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Default monitor '{}' not found, will use monitor with lowest ID as primary monitor", g_defaultMonitor.c_str());
    }
    // default monitor not set, let's use the monitor with the lowest ID
    // but let's first filter out invalid monitors (likely will never happen I assume, but just in case)
    auto validMonitors = State::monitorState()->monitors() | std::views::filter([](const PHLMONITOR& m) { return m->m_id != MONITOR_INVALID; });
    auto const primaryMonitorIt = std::ranges::min_element(validMonitors, std::ranges::less{}, [](const PHLMONITOR& m) { return m->m_id; });
    if (primaryMonitorIt != validMonitors.end()) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Using monitor '{}' with lowest ID {} as primary monitor", (*primaryMonitorIt)->m_name.c_str(), (*primaryMonitorIt)->m_id);
        return *primaryMonitorIt;
    }
    Log::logger->log(Log::ERR, "[split-monitor-workspaces] No valid monitors found?");
    return nullptr; // we don't throw here because this can sometimes happen when waking from suspend
}

const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace)
{
    // based on the string, we parse multiple formats:
    // #1 - "empty" -> get the first empty workspace on the monitor, or the last workspace if all have windows
    // #2 - "+1", "-2" -> relative workspace ID, e.g. next or previous workspace
    // #3 - "1", "2", "3" -> absolute workspace ID, e.g. workspace 1, 2 or 3 on the current monitor
    // if these formats fail to be parsed from the workspace string, we assume the user wants to switch to a workspace by name and simply pass that to hyprland

    auto const curWorkspacesIt = g_vMonitorWorkspaceMap.find(monitor->m_id);
    if (curWorkspacesIt == g_vMonitorWorkspaceMap.end()) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor ID {} not found in workspace map", monitor->m_id);
        return workspace; // use the original string if the monitor is not mapped
    }
    const std::vector<std::string>& curWorkspaces = curWorkspacesIt->second;
    if (curWorkspaces.empty()) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] No workspaces mapped to monitor ID {}", monitor->m_id);
        return workspace; // use the original string if no workspaces are mapped
    }

    // #1 if the workspace is "empty", we expect the new ID to be the first available ID on the given monitor (not the first ID in the global list)
    if (workspace == "empty") {
        // get the next workspace ID that is empty on this monitor
        for (const auto& workspaceName : curWorkspaces) {
            PHLWORKSPACE workspacePtr = State::workspaceState()->query().name(workspaceName).run();
            // the workspace we want is either not yet created (=nullptr) or already created but empty (!= nullptr but no windows)
            if (workspacePtr == nullptr || workspacePtr->getWindowCount() == 0) {
                return workspaceName;
            }
        }
        // if no empty monitor, we just go to the last workspace in the map
        return curWorkspaces.back();
    }

    int workspaceIndex = 0;
    if (workspace.starts_with("+") || workspace.starts_with("-")) {
        // #2 relative IDS, e.g. +1, -2
        auto delta = directionToDelta(workspace);
        if (delta == 0) {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] Invalid workspace delta: {}", workspace.c_str());
            return workspace;
        }
        // find the current workspace index in the monitor's workspace list
        auto it = std::ranges::find(curWorkspaces, monitor->m_activeWorkspace->m_name);
        if (it == curWorkspaces.end()) {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] Current workspace {} not found in monitor workspaces", monitor->m_activeWorkspace->m_name.c_str());
            return workspace;
        }
        workspaceIndex = std::distance(curWorkspaces.begin(), it) + delta;
    }
    else {
        // #3 absolute IDs, e.g. 1, 2, 3
        try {
            // convert to 0-indexed int
            workspaceIndex = std::stoi(workspace) - 1;
        }
        catch (std::invalid_argument&) {
            // if parsing fails, assume the user wants to switch to the workspace by name
            Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid workspace index: {}, assuming named workspace", workspace.c_str());
            return workspace;
        }
    }

    if (workspaceIndex < 0) {
        if (g_config.enableWrapping->value()) {
            return curWorkspaces.back(); // wrap around to the last workspace
        }
        return curWorkspaces.front(); // stop at the first workspace
    }

    if (static_cast<size_t>(workspaceIndex) >= curWorkspaces.size()) {
        if (g_config.enableWrapping->value()) {
            return curWorkspaces.front(); // wrap around to the first workspace
        }
        return curWorkspaces.back(); // stop at the last workspace
    }

    return curWorkspaces[workspaceIndex];
}

PHLMONITOR getCurrentMonitor()
{
    // get last focused monitor, because some people switch monitors with a keybind while the cursor is on a different monitor
    if (PHLMONITOR monitor = Desktop::focusState()->monitor()) {
        return monitor;
    }
    Log::logger->log(Log::WARN, "[split-monitor-workspaces] Last monitor does not exist, falling back to cursor's monitor");
    // fallback to the monitor the cursor is on
    return State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
}

int64_t calcWorkspaceBaseIndex(const std::string& name)
{
    int64_t currentPriority = g_vMonitorPriorities[name];

    int64_t offset = 0;
    for (const auto& [n, p] : g_vMonitorPriorities) {
        if (p < currentPriority) {
            offset += getMonitorMaxWorkspaces(n);
        }
    }

    return offset;
}

std::string configTypeToString(Config::eConfigManagerType type)
{
    switch (type) {
        case Config::CONFIG_LEGACY:
            return "legacy";
        case Config::CONFIG_LUA:
            return "lua";
        default:
            return "unknown";
    }
}

std::optional<std::string> luaArgToString(lua_State* L, int idx)
{
    if (lua_isinteger(L, idx) != 0)
        return std::to_string(lua_tointeger(L, idx));
    const auto* str = lua_tostring(L, idx);
    if (str == nullptr)
        return std::nullopt;
    return std::string{str};
}

int pushLuaDispatchResult(lua_State* L, const SDispatchResult& result)
{
    lua_newtable(L);
    lua_pushboolean(L, static_cast<int>(result.success));
    lua_setfield(L, -2, "ok");
    lua_pushboolean(L, static_cast<int>(result.passEvent));
    lua_setfield(L, -2, "pass_event");
    if (!result.success) {
        lua_pushstring(L, result.error.c_str());
        lua_setfield(L, -2, "error");
    }
    return 1;
}

int pushLuaArgError(lua_State* L, const std::string& fn)
{
    return pushLuaDispatchResult(L, {.success = false, .error = fn + " expects a string or integer argument"});
}
