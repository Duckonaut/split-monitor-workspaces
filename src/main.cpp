#include <algorithm>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspacePlacementController.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprutils/string/VarList2.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include "globals.hpp"
#include "helpers.hpp"

namespace {

SDispatchResult splitWorkspace(const std::string& workspace)
{
    if (!g_config.linkMonitors->value()) {
        // not linked => just change workspace on current monitor
        auto const result = runDispatcher("workspace", getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
        return {.success = result == "ok", .error = result};
    }
    // workspaces are linked => change workspace on all monitors
    std::vector<SDispatchResult> results;
    for (const PHLMONITOR& monitor : State::monitorState()->monitors()) {
        bool noFocus = monitor != getCurrentMonitor(); // only focus the current monitor
        auto workspaceRef = State::workspaceState()->query().name(getWorkspaceFromMonitor(monitor, workspace)).run();
        if (workspaceRef.get() == nullptr) {
            // create it if it doesn't exist yet
            auto const workspaceID = getWorkspaceIDNameFromString(getWorkspaceFromMonitor(monitor, workspace)).id;
            workspaceRef = State::workspaceState()->create(workspaceID, monitor->m_id);
        }
        monitor->changeWorkspace(workspaceRef, false, true, noFocus);
    }
    return {.success = true, .error = ""};
}

SDispatchResult cycleWorkspaces(const std::string& value, bool nowrap = false)
{
    int const delta = directionToDelta(value);
    if (delta == 0) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid cycle value: {}", value.c_str());
        return {.success = false, .error = "Invalid cycle value: " + value};
    }

    std::vector<PHLMONITOR> monitorsToCycle;
    if (g_config.linkMonitors->value())
        monitorsToCycle = State::monitorState()->monitors();
    else
        monitorsToCycle = std::vector<PHLMONITOR>{getCurrentMonitor()};

    for (const PHLMONITOR& monitor : monitorsToCycle) {
        Log::logger->log(Log::DEBUG, "[split-monitor-workspaces] Cycling workspace on monitor {} (ID {}) by {}", monitor->m_name, monitor->m_id, delta);
        auto const workspaces = g_vMonitorWorkspaceMap[monitor->m_id];
        int index = -1;
        for (int i = 0; i < getMonitorMaxWorkspaces(monitor->m_name); i++) {
            if (workspaces[i] == monitor->m_activeWorkspace->m_name) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            Log::logger->log(Log::WARN, "[split-monitor-workspaces] Could not find active workspace in monitor workspaces. Aborting cycle.");
            return {.success = false, .error = "Could not find active workspace in monitor workspaces"};
        }

        index += delta;
        if (index < 0) {
            if (nowrap) {
                return {.success = true, .error = ""}; // null operation because wrapping is disabled
            }
            index = getMonitorMaxWorkspaces(monitor->m_name) - 1; // wrap around to the last workspace
        }
        else if (index >= getMonitorMaxWorkspaces(monitor->m_name)) {
            if (nowrap) {
                return {.success = true, .error = ""}; // null operation because wrapping is disabled
            }
            index = 0; // wrap around to the first workspace
        }
        auto workspaceRef = State::workspaceState()->query().name(workspaces[index]).run();
        if (workspaceRef.get() == nullptr) {
            // create it if it doesn't exist yet
            auto const workspaceID = getWorkspaceIDNameFromString(workspaces[index]).id;
            workspaceRef = State::workspaceState()->create(workspaceID, monitor->m_id);
        }
        monitor->changeWorkspace(workspaceRef, false, true, monitor != getCurrentMonitor());
    }
    return {.success = true, .error = ""};
}

SDispatchResult splitCycleWorkspaces(const std::string& value)
{
    return cycleWorkspaces(value, !g_config.enableWrapping->value());
}

SDispatchResult splitMoveToWorkspace(const std::string& workspace)
{
    if (!g_config.linkMonitors->value()) {
        // not linked => just move to workspace on current monitor
        auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), false);
        return {.success = result == "ok", .error = result};
    }
    // workspaces are linked => silently move to workspace, then change workspace on all monitors
    auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), true);
    splitWorkspace(workspace);
    return {.success = result == "ok", .error = result};
}

SDispatchResult splitMoveToWorkspaceSilent(const std::string& workspace)
{
    auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), true);
    return {.success = result == "ok", .error = result};
}

SDispatchResult changeMonitor(bool quiet, const std::string& value)
{
    PHLMONITOR monitor = getCurrentMonitor();

    PHLMONITOR nextMonitor = nullptr;

    const auto& monitorsVec = State::monitorState()->monitors();
    uint64_t monitorCount = monitorsVec.size();

    int const delta = directionToDelta(value);
    if (delta == 0) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid monitor value: {}", value.c_str());
        return {.success = false, .error = "Invalid monitor value: " + value};
    }

    // The index is used instead of the monitorID because using the monitorID won't work if monitors are removed or mirrored
    // as there would be gaps in the monitorID sequence
    int currentMonitorIndex = -1;
    for (size_t i = 0; i < monitorsVec.size(); i++) {
        if (monitorsVec[i] == monitor) {
            currentMonitorIndex = i;
            break;
        }
    }
    if (currentMonitorIndex == -1) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor ID {} not found in monitor list?", monitor->m_id);
        return {.success = false, .error = "Monitor ID not found in monitor list: " + std::to_string(monitor->m_id)};
    }

    int nextMonitorIndex = (monitorCount + currentMonitorIndex + delta) % monitorCount;

    nextMonitor = monitorsVec[nextMonitorIndex];

    int nextWorkspaceID = nextMonitor->m_activeWorkspace->m_id;

    std::string result;
    if (quiet) {
        result = dispatchMoveToWorkspace(std::to_string(nextWorkspaceID), true);
    }
    else {
        result = dispatchMoveToWorkspace(std::to_string(nextWorkspaceID), false);
    }
    return {.success = result == "ok", .error = result};
}

SDispatchResult splitChangeMonitorSilent(const std::string& value)
{
    return changeMonitor(true, value);
}

SDispatchResult splitChangeMonitor(const std::string& value)
{
    return changeMonitor(false, value);
}

SDispatchResult grabRogueWindows(const std::string& /*unused*/)
{
    // implementation loosely based on shezdy's hyprsplit: https://github.com/shezdy/hyprsplit
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Grabbing rogue windows");
    const auto currentMonitor = getCurrentMonitor();
    if (currentMonitor == nullptr) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] No active monitor found");
        return {.success = false, .error = "No active monitor found"};
    }
    const auto currentWorkspace = currentMonitor->m_activeWorkspace;
    if (currentWorkspace == nullptr) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] No active workspace found");
        return {.success = false, .error = "No active workspace found"};
    }

    for (const auto& window : Desktop::windowState()->windows()) {
        // ignore unmapped and special windows
        if (!window->m_isMapped && !window->onSpecialWorkspace())
            continue;

        auto const workspaceName = window->m_workspace->m_name;
        auto const monitorID = window->m_monitor->m_id;

        bool isInRogueWorkspace = !g_vMonitorWorkspaceMap.contains(monitorID) || // if the monitor is not mapped, the window is rogue
                                  !std::ranges::any_of(g_vMonitorWorkspaceMap[monitorID], [&workspaceName](const auto& mappedWorkspaceName) { return workspaceName == mappedWorkspaceName; });
        if (isInRogueWorkspace) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Moving rogue window {} from workspace {} to workspace {}", window->m_title.c_str(), workspaceName.c_str(),
                             currentWorkspace->m_name.c_str());
            Desktop::globalWindowController()->moveWindowToWorkspace(window, currentWorkspace);
        }
    }
    return {.success = true, .error = ""};
}

void mapMonitor(const PHLMONITOR& monitor) // NOLINT(readability-convert-member-functions-to-
{
    if (monitor->m_activeMonitorRule.m_disabled) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Skipping disabled monitor {}", monitor->m_name);
        return;
    }

    if (monitor->isMirror()) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Skipping mirrored monitor {}", monitor->m_name);
        return;
    }

    // determine monitor priority if not set
    if (!g_vMonitorPriorities.contains(monitor->m_name)) {
        g_vMonitorPriorities[monitor->m_name] = static_cast<int64_t>(g_vMonitorPriorities.size());
    }

    int64_t workspaceIndex = calcWorkspaceBaseIndex(monitor->m_name);
    int64_t maxWorkspaces = getMonitorMaxWorkspaces(monitor->m_name);
    workspaceIndex += 1;

    Log::logger->log(Log::INFO, "{}",
                     "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + maxWorkspaces - 1) + " to monitor " + monitor->m_name);

    for (int i = workspaceIndex; i < workspaceIndex + maxWorkspaces; i++) {
        std::string workspaceName = std::to_string(i);
        g_vMonitorWorkspaceMap[monitor->m_id].push_back(workspaceName);
        PHLWORKSPACE workspace = State::workspaceState()->query().name(workspaceName).run();

        // when not using persistent workspaces, we still want to create the first workspace on each monitor
        // to avoid issues where only the last mapped monitor has the correct workspace (#121)
        if (workspace.get() == nullptr && (g_config.enablePersistentWorkspaces->value() || i == workspaceIndex)) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Creating workspace {}", workspaceName);
            workspace = State::workspaceState()->create(i, monitor->m_id);
        }
        if (workspace.get() != nullptr) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Moving workspace {} to monitor {}", workspaceName, monitor->m_name);
            State::workspacePlacementController()->moveWorkspaceToMonitor(workspace, monitor);

            if (g_config.enablePersistentWorkspaces->value()) {
                workspace->setPersistent(true);
                g_vPersistentWorkspaces.push_back(workspace); // keep a reference to avoid it being destructed (see https://github.com/hyprwm/Hyprland/discussions/11400#discussioncomment-14085672)
            }
            else {
                // if this is the first workspace on the monitor, we still want to make sure it's focused on startup
                // this is to avoid issues where the workspace is destroyed right after we move it (#220)
                if (i == workspaceIndex && g_firstLoad) {
                    monitor->changeWorkspace(workspace, false, true, true);
                }
            }
        }
    }

    if (!g_config.keepFocused->value() || g_firstLoad) {
        // we also want to switch to the first workspace when the plugin is first loaded
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Switching to first workspace {} on monitor {}", std::to_string(workspaceIndex), monitor->m_name);
        runDispatcher("workspace", std::to_string(workspaceIndex));
    }
}

void unmapMonitor(const PHLMONITOR& monitor)
{
    int64_t workspaceIndex = calcWorkspaceBaseIndex(monitor->m_name);
    int64_t maxWorkspaces = getMonitorMaxWorkspaces(monitor->m_name);
    workspaceIndex += 1;

    Log::logger->log(Log::INFO, "{}",
                     "[split-monitor-workspaces] Unmapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + maxWorkspaces - 1) + " from monitor " +
                         monitor->m_name);

    if (g_vMonitorWorkspaceMap.contains(monitor->m_id)) {
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitor->m_id]) {
            PHLWORKSPACE workspace = State::workspaceState()->query().name(workspaceName).run();

            if (workspace.get() != nullptr) {
                workspace->setPersistent(false);
                // remove this workspace shared ptr from the persistent workspaces vector, so it can be destructed if no other references exist
                std::erase(g_vPersistentWorkspaces, workspace);
            }
        }
        g_vMonitorWorkspaceMap.erase(monitor->m_id);
    }

    if (g_vMonitorPriorities.contains(monitor->m_name) && !g_vMonitorPriorities[monitor->m_name].wasSetFromConfig) {
        g_vMonitorPriorities.erase(monitor->m_name);
    }

    if (g_vMonitorMaxWorkspaces.contains(monitor->m_name) && !g_vMonitorMaxWorkspaces[monitor->m_name].wasSetFromConfig) {
        g_vMonitorMaxWorkspaces.erase(monitor->m_name);
    }
}

void unmapAllMonitors()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Unmapping all monitors");
    try {
        while (!g_vMonitorWorkspaceMap.empty()) {
            auto [monitorID, workspaces] = *g_vMonitorWorkspaceMap.begin();
            PHLMONITOR monitor = State::monitorState()->query().id(monitorID).run();
            if (monitor != nullptr) {
                unmapMonitor(monitor); // will remove the monitor from the map
            }
            else {
                g_vMonitorWorkspaceMap.erase(monitorID); // remove it manually
            }
        }
    }
    catch (const std::exception& e) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Exception while unmapping monitors: {}", e.what());
    }
    g_vMonitorWorkspaceMap.clear();
    g_vPersistentWorkspaces.clear();
}

void remapAllMonitors()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Remapping all monitors");
    raiseNotification("[split-monitor-workspaces] Remapping workspaces...");
    unmapAllMonitors();
    for (const PHLMONITOR& monitor : State::monitorState()->monitors()) {
        mapMonitor(monitor);
    }
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Mapped all monitors");
    // if keepFocused is false or first load, switch to the first workspace on the default or first monitor
    if (!g_config.keepFocused->value() || g_firstLoad) {
        if (!State::monitorState()->monitors().empty()) {
            PHLMONITOR primaryMonitor = getPrimaryMonitor();
            if (primaryMonitor == nullptr) {
                Log::logger->log(Log::ERR, "[split-monitor-workspaces] No primary monitor found?");
                return;
            }
            if (!g_vMonitorWorkspaceMap.contains(primaryMonitor->m_id)) {
                Log::logger->log(Log::ERR, "[split-monitor-workspaces] Primary monitor ID {} not found in workspace map?", primaryMonitor->m_id);
                return;
            }
            if (!g_vMonitorWorkspaceMap[primaryMonitor->m_id].empty()) {
                std::string firstWorkspace = g_vMonitorWorkspaceMap[primaryMonitor->m_id][0];
                Log::logger->log(Log::INFO, "[split-monitor-workspaces] Switching to first workspace {} on first monitor {}", firstWorkspace, primaryMonitor->m_name);
                runDispatcher("workspace", firstWorkspace);
            }
        }
        else {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] No monitors found?");
        }
    }
}

void loadMonitorPriority(const std::string& raw)
{
    if (raw.empty())
        return;

    const auto args = Hyprutils::String::CVarList2(raw.c_str());

    int64_t priorityCounter = 0;
    for (const auto& arg : args) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor priority: {} -> {}", arg, priorityCounter);
        g_vMonitorPriorities[std::string(arg)] = {.value = priorityCounter, .wasSetFromConfig = true};
        priorityCounter++;
    }
}

void loadMonitorMaxWorkspaces(const std::string& raw)
{
    if (raw.empty())
        return;

    size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find(';', start);
        const auto entry = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);

        if (!entry.empty()) {
            const auto args = Hyprutils::String::CVarList2(entry.c_str());
            if (args.size() != 2) {
                Log::logger->log(Log::ERR, "[split-monitor-workspaces] Invalid max_workspaces entry '{}', expected 'MONITOR, COUNT' entries separated by ';'", entry.c_str());
            }
            else {
                try {
                    auto monitorName = std::string(args[0]);
                    const auto maxWorkspaces = std::stoi(std::string(args[1]));

                    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor max workspaces from Lua config: {} -> {}", monitorName.c_str(), maxWorkspaces);
                    g_vMonitorMaxWorkspaces[monitorName] = {.value = maxWorkspaces, .wasSetFromConfig = true};
                }
                catch (const std::exception& e) {
                    Log::logger->log(Log::ERR, "[split-monitor-workspaces] Failed to parse max_workspaces entry '{}': {}", entry.c_str(), e.what());
                }
            }
        }

        if (end == std::string::npos)
            break;

        start = end + 1;
    }
}

void loadConfigValues()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Loading config values");

    try {
        auto defaultMonitor = getConfigValue<Config::STRING>(translateConfigKey(k_defaultMonitor));
        if (defaultMonitor.has_value()) {
            g_defaultMonitor = defaultMonitor.value();
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Default monitor from config: '{}'", g_defaultMonitor);
        }
        else {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] No default monitor specified in config");
        }
    }
    catch (const std::exception& e) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Failed to get default monitor from config: {}", e.what());
    }

    try {
        if (g_config.enableHy3->value()) {
            g_hy3Status = Hy3Status::DETECTION_PENDING; // reset so it re-checks on next use
        }
        else {
            g_hy3Status = Hy3Status::DISABLED;
        }
    }
    catch (const std::exception& e) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Exception while loading Hy3 status from config: {}", e.what());
    }

    if (Config::mgr()->type() == Config::CONFIG_LUA) {
        try {
            auto monitorPriority = getConfigValue<Config::STRING>(translateConfigKey(k_monitorPriority));
            if (monitorPriority.has_value()) {
                loadMonitorPriority(monitorPriority.value());
            }
            auto monitorMaxWorkspaces = getConfigValue<Config::STRING>(translateConfigKey(k_monitorMaxWorkspaces));
            if (monitorMaxWorkspaces.has_value()) {
                loadMonitorMaxWorkspaces(monitorMaxWorkspaces.value());
            }
        }
        catch (const std::exception& e) {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] Exception while loading monitor configuration: {}", e.what());
        }
    }
}

void reload()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Reloading plugin configuration");
    try {
        loadConfigValues();
        remapAllMonitors();
    }
    catch (const std::exception& e) {
        raiseNotification("[split-monitor-workspaces] Failed to reload config, see logs for details");
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Exception during reload: {}", e.what());
    }
    g_firstLoad = false;
}

void monitorAddedCallback(const PHLMONITOR& monitor)
{ // NOLINT(performance-unnecessary-value-param)
    if (monitor == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor added callback called with nullptr?");
        return;
    }
    mapMonitor(monitor);
}

void monitorRemovedCallback(PHLMONITOR monitor) // NOLINT(performance-unnecessary-value-param)
{
    if (monitor == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor removed callback called with nullptr?");
        return;
    }
    unmapMonitor(monitor);
}

void configReloadedCallback() // NOLINT(performance-unnecessary-value-param)
{
    // !!! anything you call in this function should not reload the config, as it will cause an infinite loop !!!
    try {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Config reloaded");
        raiseNotification("[split-monitor-workspaces] Config reloaded");
        reload();
    }
    catch (const std::exception& e) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Exception during config reload: {}", e.what());
        try {
            unmapAllMonitors();
        }
        catch (...) {
        }
    }
}

void preConfigReloadCallback() // NOLINT(performance-unnecessary-value-param)
{
    // clear monitor-specific config values. This is needed if the user
    // removes monitor_priority or monitor_max_workspaces entries from
    // the config. Without this, the old values would persist.
    g_vMonitorPriorities.clear();
    g_vMonitorMaxWorkspaces.clear();
}

Hyprlang::CParseResult monitorPriorityConfigHandler(const char* command, const char* args)
{
    const auto ARGS = Hyprutils::String::CVarList2(args);

    int64_t priorityCounter = 0;
    for (const auto& arg : ARGS) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor priority: {} -> {}", arg, priorityCounter);
        g_vMonitorPriorities[std::string(arg)] = {.value = priorityCounter, .wasSetFromConfig = true};
        priorityCounter++;
    }

    Hyprlang::CParseResult result;
    return result;
}

Hyprlang::CParseResult monitorMaxWorkspacesConfigHandler(const char* command, const char* args)
{
    const auto ARGS = Hyprutils::String::CVarList2(args);

    if (ARGS.size() != 2) {
        Hyprlang::CParseResult result;
        std::string errorMsg = "[split-monitor-workspaces] Invalid number of arguments, expected 2 (name, maxWorkspaces)";
        Log::logger->log(Log::ERR, errorMsg);
        result.setError(errorMsg.c_str());
        return result;
    }

    std::string parseError;

    try {
        auto const monitorName = std::string(ARGS[0]);
        const int maxWorkspaces = std::stoi(std::string(ARGS[1]));

        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor max workspaces: {} -> {}", monitorName.c_str(), maxWorkspaces);
        g_vMonitorMaxWorkspaces[monitorName] = {.value = maxWorkspaces, .wasSetFromConfig = true};
    }
    catch (...) {
        parseError = "[split-monitor-workspaces] Failed to parse monitor max workspaces";
    }

    Hyprlang::CParseResult result;
    if (!parseError.empty()) {
        Log::logger->log(Log::ERR, parseError);
        result.setError(parseError.c_str());
    }
    return result;
}

int luaWorkspace(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitWorkspace(*arg)) : pushLuaArgError(L, "workspace");
}

int luaCycleWorkspaces(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitCycleWorkspaces(*arg)) : pushLuaArgError(L, "cycle_workspaces");
}

int luaMoveToWorkspace(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitMoveToWorkspace(*arg)) : pushLuaArgError(L, "move_to_workspace");
}

int luaMoveToWorkspaceSilent(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitMoveToWorkspaceSilent(*arg)) : pushLuaArgError(L, "move_to_workspace_silent");
}

int luaChangeMonitor(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitChangeMonitor(*arg)) : pushLuaArgError(L, "change_monitor");
}

int luaChangeMonitorSilent(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitChangeMonitorSilent(*arg)) : pushLuaArgError(L, "change_monitor_silent");
}

int luaGrabRogueWindows(lua_State* L)
{
    return pushLuaDispatchResult(L, grabRogueWindows(""));
}

int monitorPriorityLuaHandler(lua_State* L)
{
    if (!lua_istable(L, 1))
        return Config::Lua::Bindings::Internal::configError(L, "monitor_priority: expected a table of monitor name strings");

    int64_t priorityCounter = 0;
    int n = lua_rawlen(L, 1);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_isstring(L, -1) == 0) {
            lua_pop(L, 1);
            return Config::Lua::Bindings::Internal::configError(L, "monitor_priority: all elements must be strings");
        }
        std::string monitorName = lua_tostring(L, -1);
        lua_pop(L, 1);
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor priority: {} -> {}", monitorName, priorityCounter);
        g_vMonitorPriorities[monitorName] = {.value = priorityCounter, .wasSetFromConfig = true};
        priorityCounter++;
    }
    return 0;
}

int monitorMaxWorkspacesLuaHandler(lua_State* L)
{
    if (!lua_istable(L, 1))
        return Config::Lua::Bindings::Internal::configError(L, "max_workspaces: expected a table { monitor, max }");

    std::string monitorName;
    int64_t maxWorkspaces = 0;

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
        lua_getfield(L, 1, "monitor");
        if (lua_isstring(L, -1) == 0)
            return Config::Lua::Bindings::Internal::configError(L, "max_workspaces: monitor must be a string");
        monitorName = lua_tostring(L, -1);
    }

    {
        Hyprutils::Utils::CScopeGuard x([L] { lua_pop(L, 1); });
        lua_getfield(L, 1, "max");
        if (lua_isinteger(L, -1) == 0)
            return Config::Lua::Bindings::Internal::configError(L, "max_workspaces: max must be an integer");
        maxWorkspaces = lua_tointeger(L, -1);
    }

    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor max workspaces: {} -> {}", monitorName, maxWorkspaces);
    g_vMonitorMaxWorkspaces[monitorName] = {.value = maxWorkspaces, .wasSetFromConfig = true};
    return 0;
}

} // namespace

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Initializing plugin");
    PHANDLE = handle;

    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Creating config values");
    g_config.workspaceCount = Config::Values::makeConfigValue<Config::Values::Int>(translateConfigKey(k_workspaceCount), "How many workspaces to bind to the monitor", 10);
    g_config.keepFocused = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_keepFocused), "Keep current workspaces focused on plugin init/reload", false);
    g_config.enableNotifications = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_enableNotifications), "Enable plugin notifications", false);
    g_config.enablePersistentWorkspaces = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_enablePersistentWorkspaces), "Enable persistent workspaces", true);
    g_config.enableWrapping = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_enableWrapping), "Enable workspace wrapping", true);
    g_config.linkMonitors = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_linkMonitors), "Enable gnome-like workspace switching", false);
    g_config.enableHy3 = Config::Values::makeConfigValue<Config::Values::Bool>(translateConfigKey(k_enableHy3), "Enable Hy3 support", true);

    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Adding config values to Hyprland");
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.workspaceCount);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.keepFocused);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.enableNotifications);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.enablePersistentWorkspaces);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.enableWrapping);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.linkMonitors);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_config.enableHy3);

    auto const configType = Config::mgr()->type();
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Detected config type: {}", configTypeToString(configType));
    switch (configType) {
        case Config::CONFIG_LEGACY: {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Adding config keywords to Hyprland (legacy)");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            HyprlandAPI::addConfigKeyword(PHANDLE, translateConfigKey(k_monitorPriority), monitorPriorityConfigHandler, (Hyprlang::SHandlerOptions){.allowFlags = false});
            HyprlandAPI::addConfigKeyword(PHANDLE, translateConfigKey(k_monitorMaxWorkspaces), monitorMaxWorkspacesConfigHandler, (Hyprlang::SHandlerOptions){.allowFlags = false});
#pragma GCC diagnostic pop

            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Registering dispatchers");
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-workspace", splitWorkspace);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-cycleworkspaces", splitCycleWorkspaces);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitor", splitChangeMonitor);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);
            HyprlandAPI::addDispatcherV2(PHANDLE, "split-grabroguewindows", grabRogueWindows);
            break;
        }
        case Config::CONFIG_LUA: {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Registering Lua functions");
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "monitor_priority", monitorPriorityLuaHandler);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "max_workspaces", monitorMaxWorkspacesLuaHandler);

            // dispatchers
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "workspace", luaWorkspace);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "cycle_workspaces", luaCycleWorkspaces);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "move_to_workspace", luaMoveToWorkspace);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "move_to_workspace_silent", luaMoveToWorkspaceSilent);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "change_monitor", luaChangeMonitor);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "change_monitor_silent", luaChangeMonitorSilent);
            HyprlandAPI::addLuaFunction(PHANDLE, "split_monitor_workspaces", "grab_rogue_windows", luaGrabRogueWindows);
            break;
        }
        default: {
            HyprlandAPI::addNotification(PHANDLE, "[split-monitor-workspaces] Failure in initialization: Failed to get a valid config manager", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
            throw std::runtime_error("[split-monitor-workspaces] Failure in initialization: Failed to get a valid config manager");
        }
    }

    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Registering event listeners");
    e_monitorAddedHandle = Event::bus()->m_events.monitor.added.listen(monitorAddedCallback);
    e_monitorRemovedHandle = Event::bus()->m_events.monitor.removed.listen(monitorRemovedCallback);
    e_configReloadedHandle = Event::bus()->m_events.config.reloaded.listen(configReloadedCallback);
    e_preConfigReloadHandle = Event::bus()->m_events.config.preReload.listen(preConfigReloadCallback);

    // config loading and initial mapping of the workspaces will happen after plugin initialization, through the configReloadedCallback.
    // this is because Hyprland will automatically force a config reload after the plugin is loaded

    raiseNotification("[split-monitor-workspaces] Initialized successfully!");
    return {.name = "split-monitor-workspaces", .description = "Split monitor workspace namespaces", .author = "zjeffer", .version = "1.2.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    unmapAllMonitors();
    raiseNotification("[split-monitor-workspaces] Unloaded successfully!");

    // TODO: should we unregister added lua dispatchers & functions here?
}
