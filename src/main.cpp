#include <cstddef>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include "globals.hpp"
#include "util.hpp"

#include <map>
#include <unistd.h>
#include <vector>

auto constexpr k_workspaceCount = "plugin:split-monitor-workspaces:count";
auto constexpr k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
auto constexpr k_enableNotifications = "plugin:split-monitor-workspaces:enable_notifications";
auto constexpr k_enablePersistentWorkspaces = "plugin:split-monitor-workspaces:enable_persistent_workspaces";
auto constexpr k_enableWrapping = "plugin:split-monitor-workspaces:enable_wrapping";

static const CHyprColor s_pluginColor = {0x61 / 255.0F, 0xAF / 255.0F, 0xEF / 255.0F, 1.0F};

static int g_workspaceCount;
static bool g_keepFocused = false;
static bool g_enableNotifications = false;
static bool g_enablePersistentWorkspaces = true;
static bool g_enableWrapping = true;

// the first time we load the plugin, we want to switch to the first workspace on the first monitor regardless of keepFocused
static bool g_firstLoad = true;

static std::map<std::string, std::vector<std::string>> g_vMonitorWorkspaceMap;
static std::vector<PHLWORKSPACE> g_vPersistentWorkspaces;

static SP<HOOK_CALLBACK_FN> e_monitorAddedHandle = nullptr;
static SP<HOOK_CALLBACK_FN> e_monitorRemovedHandle = nullptr;
static SP<HOOK_CALLBACK_FN> e_configReloadedHandle = nullptr;

static void raiseNotification(const std::string& message, float timeout = 5000.0F)
{
    if (g_enableNotifications) {
        HyprlandAPI::addNotification(PHANDLE, message, s_pluginColor, timeout);
    }
}

static int getDelta(const std::string& direction)
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
        Debug::log(WARN, "[split-monitor-workspaces] Invalid direction value: {}", direction.c_str());
    }
    // fallback if input is incorrect
    return 0;
}

static int getParamValue(const char* paramName)
{
    Debug::log(INFO, "[split-monitor-workspaces] Getting config value {}", paramName);
    const auto* const paramPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, paramName)->getDataStaticPtr();
    if (paramPtr == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to get config value {}", paramName);
        return 0;
    }
    return **paramPtr;
}

static const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace)
{
    // based on the string, we parse multiple formats:
    // #1 - "empty" -> get the first empty workspace on the monitor, or the last workspace if all have windows
    // #2 - "+1", "-2" -> relative workspace ID, e.g. next or previous workspace
    // #3 - "1", "2", "3" -> absolute workspace ID, e.g. workspace 1, 2 or 3 on the current monitor
    // if these formats fail to be parsed form the workspace string, we assume the user wants to switch to a workspace by name and simply pass that to hyprland

    auto const curWorkspacesIt = g_vMonitorWorkspaceMap.find(getMonitorIdentifier(monitor));
    if (curWorkspacesIt == g_vMonitorWorkspaceMap.end()) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor ID {} not found in workspace map", getMonitorIdentifier(monitor).c_str());
        return workspace; // use the original string if the monitor is not mapped
    }
    const std::vector<std::string>& curWorkspaces = curWorkspacesIt->second;
    if (curWorkspaces.empty()) {
        Debug::log(WARN, "[split-monitor-workspaces] No workspaces mapped to monitor ID {}", getMonitorIdentifier(monitor).c_str());
        return workspace; // use the original string if no workspaces are mapped
    }

    // #1 if the workspace is "empty", we expect the new ID to be the first available ID on the given monitor (not the first ID in the global list)
    if (workspace == "empty") {
        // get the next workspace ID that is empty on this monitor
        for (const auto& workspaceName : curWorkspaces) {
            PHLWORKSPACE workspacePtr = g_pCompositor->getWorkspaceByName(workspaceName);
            // the workspace we want is either not yet created (=nullptr) or already created but empty (!= nullptr but no windows)
            if (workspacePtr == nullptr || workspacePtr->getWindows() == 0) {
                return workspaceName;
            }
        }
        // if no empty monitor, we just go to the last workspace in the map
        return curWorkspaces.back();
    }

    int workspaceIndex = 0;
    if (workspace.starts_with("+") || workspace.starts_with("-")) {
        // #2 relative IDS, e.g. +1, -2
        auto delta = getDelta(workspace);
        if (delta == 0) {
            Debug::log(ERR, "[split-monitor-workspaces] Invalid workspace delta: {}", workspace.c_str());
            return workspace;
        }
        // find the current workspace index in the monitor's workspace list
        auto it = std::ranges::find(curWorkspaces, monitor->m_activeWorkspace->m_name);
        if (it == curWorkspaces.end()) {
            Debug::log(ERR, "[split-monitor-workspaces] Current workspace {} not found in monitor workspaces", monitor->m_activeWorkspace->m_name.c_str());
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
            Debug::log(WARN, "[split-monitor-workspaces] Invalid workspace index: {}, assuming named workspace", workspace.c_str());
            return workspace;
        }
    }

    if (workspaceIndex < 0) {
        if (g_enableWrapping) {
            return curWorkspaces.back(); // wrap around to the last workspace
        }
        return curWorkspaces.front(); // stop at the first workspace
    }

    if ((size_t)workspaceIndex >= curWorkspaces.size()) {
        if (g_enableWrapping) {
            return curWorkspaces.front(); // wrap around to the first workspace
        }
        return curWorkspaces.back(); // stop at the last workspace
    }

    return curWorkspaces[workspaceIndex];
}

static PHLMONITOR getCurrentMonitor()
{
    // get last focused monitor, because some people switch monitors with a keybind while the cursor is on a different monitor
    if (PHLMONITOR monitor = g_pCompositor->m_lastMonitor.lock()) {
        return monitor;
    }
    Debug::log(WARN, "[split-monitor-workspaces] Last monitor does not exist, falling back to cursor's monitor");
    // fallback to the monitor the cursor is on
    return g_pCompositor->getMonitorFromCursor();
}

static SDispatchResult splitWorkspace(const std::string& workspace)
{
    auto const result = HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
    return {.success = result == "ok", .error = result};
}

static SDispatchResult cycleWorkspaces(const std::string& value, bool nowrap = false)
{
    int const delta = getDelta(value);
    if (delta == 0) {
        Debug::log(WARN, "[split-monitor-workspaces] Invalid cycle value: {}", value.c_str());
        return {.success = false, .error = "Invalid cycle value: " + value};
    }
    PHLMONITOR const monitor = getCurrentMonitor();
    auto const workspaces = g_vMonitorWorkspaceMap[getMonitorIdentifier(monitor)];
    int index = -1;
    for (int i = 0; i < g_workspaceCount; i++) {
        if (workspaces[i] == monitor->m_activeWorkspace->m_name) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        Debug::log(WARN, "[split-monitor-workspaces] Could not find active workspace in monitor workspaces. Aborting cycle.");
        return {.success = false, .error = "Could not find active workspace in monitor workspaces"};
    }

    index += delta;
    if (index < 0) {
        if (nowrap) {
            return {.success = true, .error = ""}; // null operation because wrapping is disabled
        }
        index = g_workspaceCount - 1; // wrap around to the last workspace
    }
    else if (index >= g_workspaceCount) {
        if (nowrap) {
            return {.success = true, .error = ""}; // null operation because wrapping is disabled
        }
        index = 0; // wrap around to the first workspace
    }

    auto const result = HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + workspaces[index]);
    return {.success = result == "ok", .error = result};
}

static SDispatchResult splitCycleWorkspaces(const std::string& value)
{
    return cycleWorkspaces(value, !g_enableWrapping);
}

static SDispatchResult splitCycleWorkspacesNowrap(const std::string& value)
{
    Debug::log(WARN, "[split-monitor-workspaces] split-cycleworkspacesnowrap is deprecated. Set the `enable_wrapping` config value to false instead.");
    raiseNotification("[split-monitor-workspaces] split-cycleworkspacesnowrap is deprecated. Set the `enable_wrapping` config value to false instead.");
    return cycleWorkspaces(value, true);
}

static SDispatchResult splitMoveToWorkspace(const std::string& workspace)
{
    auto const result = HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
    return {.success = result == "ok", .error = result};
}

static SDispatchResult splitMoveToWorkspaceSilent(const std::string& workspace)
{
    auto const result = HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
    return {.success = result == "ok", .error = result};
}



static PHLMONITOR getMonitorInDirectionX(PHLMONITOR pSourceMonitor, int delta) {
    if (!pSourceMonitor) {
        Debug::log(WARN, "[split-monitor-workspaces] getMonitorInDirectionX called with null source monitor");
        return nullptr;
    }

    std::vector<PHLMONITOR> monitors;
    for (const auto& m : g_pCompositor->m_monitors) {
        if (!m->isMirror()) {
            monitors.push_back(m);
        }
    }

    std::sort(monitors.begin(), monitors.end(), [](const PHLMONITOR& a, const PHLMONITOR& b) {
        return a->m_position.x < b->m_position.x;
    });

    auto it = std::find(monitors.begin(), monitors.end(), pSourceMonitor);
    if (it == monitors.end()) {
        return nullptr;
    }

    long int currentIndex = std::distance(monitors.begin(), it);
    long int nextIndex = (currentIndex + delta + monitors.size()) % monitors.size();

    return monitors[nextIndex];
}

static SDispatchResult changeMonitor(bool quiet, const std::string& value) {
    Debug::log(INFO, "[split-monitor-workspaces] changeMonitor called with quiet={}, value='{}'", quiet, value.c_str());
    PHLMONITOR monitor = getCurrentMonitor();
    if (!monitor) {
        Debug::log(WARN, "[split-monitor-workspaces] Could not get current monitor.");
        return {.success = false, .error = "Could not get current monitor"};
    }
    Debug::log(INFO, "[split-monitor-workspaces] Current monitor: {}", monitor->m_name);
    
    PHLMONITOR nextMonitor = nullptr;
    if (value == "next" || value == "prev" || value.starts_with('+') || value.starts_with('-')) {
        int delta = getDelta(value);
        nextMonitor = getMonitorInDirectionX(monitor, delta);
    }
    else {
        try {
            int monitorId = std::stoi(value);
            for (const auto& m : g_pCompositor->m_monitors) {
                if (m->m_id == monitorId) {
                    nextMonitor = m;
                    break;
                }
            }
        }
        catch (const std::invalid_argument&) {
            Debug::log(WARN, "[split-monitor-workspaces] Invalid monitor value: {}", value.c_str());
            return {.success = false, .error = "Invalid monitor value: " + value};
        }
    }

    if (!nextMonitor) {
        Debug::log(ERR, "[split-monitor-workspaces] Failed to determine next monitor.");
        return {.success = false, .error = "Failed to determine next monitor."};
    }

    // find the current workspace index in the monitor's workspace list
    auto const workspaces = g_vMonitorWorkspaceMap[getMonitorIdentifier(monitor)];
    auto it = std::ranges::find(workspaces, monitor->m_activeWorkspace->m_name);
    if (it == workspaces.end()) {
        Debug::log(ERR, "[split-monitor-workspaces] Current workspace {} not found in monitor workspaces", monitor->m_activeWorkspace->m_name.c_str());
        return {.success = false, .error = "Could not find active workspace in monitor workspaces"};
    }
    long int workspaceIndex = std::distance(workspaces.begin(), it);

    // get the corresponding workspace on the next monitor
    auto const nextWorkspaces = g_vMonitorWorkspaceMap[getMonitorIdentifier(nextMonitor)];
    if ((size_t)workspaceIndex >= nextWorkspaces.size()) {
        Debug::log(ERR, "[split-monitor-workspaces] Workspace index {} is out of bounds for the next monitor's workspaces", workspaceIndex);
        return {.success = false, .error = "Workspace index is out of bounds for the next monitor's workspaces"};
    }
    const std::string& nextWorkspaceName = nextWorkspaces[workspaceIndex];

    // Final dispatch logic
    std::string command = quiet ? "movetoworkspacesilent " : "movetoworkspace ";
    Debug::log(INFO, "[split-monitor-workspaces] Next workspace name: {}", nextWorkspaceName);
    Debug::log(INFO, "[split-monitor-workspaces] Dispatching command: '{}'", command + nextWorkspaceName);

    auto const result = HyprlandAPI::invokeHyprctlCommand("dispatch", command + nextWorkspaceName);
    return {.success = result == "ok", .error = result};
}

static SDispatchResult splitChangeMonitorSilent(const std::string& value)
{
    return changeMonitor(true, value);
}

static SDispatchResult splitChangeMonitor(const std::string& value)
{
    return changeMonitor(false, value);
}

static SDispatchResult grabRogueWindows(const std::string& /*unused*/)
{
    // implementation loosely based on shezdy's hyprsplit: https://github.com/shezdy/hyprsplit
    Debug::log(INFO, "[split-monitor-workspaces] Grabbing rogue windows");
    const auto currentMonitor = getCurrentMonitor();
    if (currentMonitor == nullptr) {
        Debug::log(ERR, "[split-monitor-workspaces] No active monitor found");
        return {.success = false, .error = "No active monitor found"};
    }
    const auto currentWorkspace = currentMonitor->m_activeWorkspace;
    if (currentWorkspace == nullptr) {
        Debug::log(ERR, "[split-monitor-workspaces] No active workspace found");
        return {.success = false, .error = "No active workspace found"};
    }

    for (const auto& window : g_pCompositor->m_windows) {
        // ignore unmapped and special windows
        if (!window->m_isMapped && !window->onSpecialWorkspace())
            continue;

        auto const workspaceName = window->m_workspace->m_name;
        PHLMONITOR monitor = window->m_monitor.lock();
        if (!monitor) {
            continue;
        }
        auto const monitorID = getMonitorIdentifier(monitor);

        bool isInRogueWorkspace = !g_vMonitorWorkspaceMap.contains(monitorID) || // if the monitor is not mapped, the window is rogue
                                  !std::ranges::any_of(g_vMonitorWorkspaceMap[monitorID], [&workspaceName](const auto& mappedWorkspaceName) { return workspaceName == mappedWorkspaceName; });
        if (isInRogueWorkspace) {
            Debug::log(INFO, "[split-monitor-workspaces] Moving rogue window {} from workspace {} to workspace {}", window->m_title.c_str(), workspaceName.c_str(), currentWorkspace->m_name.c_str());
            g_pCompositor->moveWindowToWorkspaceSafe(window, currentWorkspace);
        }
    }
    return {.success = true, .error = ""};
}

static void mapMonitor(const PHLMONITOR& monitor) // NOLINT(readability-convert-member-functions-to-static)
{
    if (monitor->m_activeMonitorRule.disabled) {
        Debug::log(INFO, "[split-monitor-workspaces] Skipping disabled monitor {}", monitor->m_name);
        return;
    }

    if (monitor->isMirror()) {
        Debug::log(INFO, "[split-monitor-workspaces] Skipping mirrored monitor {}", monitor->m_name);
        return;
    }

    std::vector<PHLMONITOR> monitors;
    for (const auto& m : g_pCompositor->m_monitors) {
        if (!m->isMirror()) {
            monitors.push_back(m);
        }
    }

    std::sort(monitors.begin(), monitors.end(), [](const PHLMONITOR& a, const PHLMONITOR& b) {
        return a->m_position.x < b->m_position.x;
    });

    auto it = std::find(monitors.begin(), monitors.end(), monitor);
    if (it == monitors.end()) {
        Debug::log(ERR, "[split-monitor-workspaces] Could not find monitor in sorted monitor list. This should not happen.");
        return;
    }

    long int monitorIndex = std::distance(monitors.begin(), it);
    int workspaceIndex = (monitorIndex * g_workspaceCount) + 1;

    Debug::log(INFO, "{}",
               "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + g_workspaceCount - 1) + " to monitor " + monitor->m_name);

    for (int i = workspaceIndex; i < workspaceIndex + g_workspaceCount; i++) {
        std::string workspaceName = std::to_string(i);
        g_vMonitorWorkspaceMap[getMonitorIdentifier(monitor)].push_back(workspaceName);
        PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

        // when not using persistent workspaces, we still want to create the first workspace on each monitor
        // to avoid issues where only the last mapped monitor has the correct workspace (#121)
        if (workspace.get() == nullptr && (g_enablePersistentWorkspaces || i == workspaceIndex)) {
            Debug::log(INFO, "[split-monitor-workspaces] Creating workspace {}", workspaceName);
            workspace = g_pCompositor->createNewWorkspace(i, monitor->m_id);
        }
        if (workspace.get() != nullptr) {
            Debug::log(INFO, "[split-monitor-workspaces] Moving workspace {} to monitor {}", workspaceName, monitor->m_name);
            g_pCompositor->moveWorkspaceToMonitor(workspace, monitor);
            if (g_enablePersistentWorkspaces) {
                workspace->setPersistent(true);
                g_vPersistentWorkspaces.push_back(workspace); // keep a reference to avoid it being destructed (see https://github.com/hyprwm/Hyprland/discussions/11400#discussioncomment-14085672)
            }
        }
    }

    if (!g_keepFocused || g_firstLoad) {
        // we also want to switch to the first workspace when the plugin is first loaded
        Debug::log(INFO, "[split-monitor-workspaces] Switching to first workspace on monitor {}", monitor->m_name);
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(workspaceIndex));
    }
}


static void unmapMonitor(const PHLMONITOR& monitor)
{
    Debug::log(INFO, "[split-monitor-workspaces] Unmapping workspaces from monitor {}", monitor->m_name);

    auto monitorIdentifier = getMonitorIdentifier(monitor);
    if (g_vMonitorWorkspaceMap.contains(monitorIdentifier)) {
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitorIdentifier]) {
            PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace.get() != nullptr) {
                workspace->setPersistent(false);
                // remove this workspace shared ptr from the persistent workspaces vector, so it can be destructed if no other references exist
                std::erase(g_vPersistentWorkspaces, workspace);
            }
        }
        g_vMonitorWorkspaceMap.erase(monitorIdentifier);
    }
}

static void unmapAllMonitors()
{
    while (!g_vMonitorWorkspaceMap.empty()) {
        auto [monitorIdentifier, workspaces] = *g_vMonitorWorkspaceMap.begin();
        PHLMONITOR monitor = nullptr;
        for (const auto& m : g_pCompositor->m_monitors) {
            if (getMonitorIdentifier(m) == monitorIdentifier) {
                monitor = m;
                break;
            }
        }

        if (monitor != nullptr) {
            unmapMonitor(monitor); // will remove the monitor from the map
        }
        else {
            g_vMonitorWorkspaceMap.erase(monitorIdentifier); // remove it manually
        }
    }
    g_vMonitorWorkspaceMap.clear();
    g_vPersistentWorkspaces.clear();
}

static void remapAllMonitors()
{
    raiseNotification("[split-monitor-workspaces] Remapping workspaces...");
    unmapAllMonitors();
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        mapMonitor(monitor);
    }
}

static void loadConfigValues()
{
    g_enableNotifications = getParamValue(k_enableNotifications) != 0;
    g_enablePersistentWorkspaces = getParamValue(k_enablePersistentWorkspaces) != 0;
    g_keepFocused = getParamValue(k_keepFocused) != 0;
    g_workspaceCount = getParamValue(k_workspaceCount);
    g_enableWrapping = getParamValue(k_enableWrapping) != 0;
}

static void reload()
{
    loadConfigValues();
    remapAllMonitors();
    g_firstLoad = false;
}

static void monitorAddedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any param)
{ // NOLINT(performance-unnecessary-value-param)
    auto monitor = std::any_cast<PHLMONITOR>(param);
    if (monitor == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor added callback called with nullptr?");
        return;
    }
    mapMonitor(monitor);
}

static void monitorRemovedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any param) // NOLINT(performance-unnecessary-value-param)
{
    auto monitor = std::any_cast<PHLMONITOR>(param);
    if (monitor == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor removed callback called with nullptr?");
        return;
    }
    unmapMonitor(monitor);
}

static void configReloadedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any /*unused*/) // NOLINT(performance-unnecessary-value-param)
{
    // anything you call in this function should not reload the config, as it will cause an infinite loop
    Debug::log(INFO, "[split-monitor-workspaces] Config reloaded");
    raiseNotification("[split-monitor-workspaces] Config reloaded");
    reload();
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    HyprlandAPI::addConfigValue(PHANDLE, k_workspaceCount, Hyprlang::INT{10});
    HyprlandAPI::addConfigValue(PHANDLE, k_keepFocused, Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, k_enableNotifications, Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, k_enablePersistentWorkspaces, Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, k_enableWrapping, Hyprlang::INT{1});

    HyprlandAPI::addDispatcherV2(PHANDLE, "split-workspace", splitWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-cycleworkspaces", splitCycleWorkspaces);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-cycleworkspacesnowrap", splitCycleWorkspacesNowrap);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitor", splitChangeMonitor);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-grabroguewindows", grabRogueWindows);

    // reload the config before adding the callback, so we can already use the config's values we defined above
    HyprlandAPI::reloadConfig();
    g_pConfigManager->reload();
    loadConfigValues();

    e_monitorAddedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", monitorAddedCallback);
    e_monitorRemovedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", monitorRemovedCallback);
    e_configReloadedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", configReloadedCallback);

    // initial mapping of the workspaces will happen after plugin initialization, through the configReloadedCallback
    // this is because Hyprland will automatically force a config reload after the plugin is loaded

    raiseNotification("[split-monitor-workspaces] Initialized successfully!");
    return {"split-monitor-workspaces", "Split monitor workspace namespaces", "Duckonaut", "1.2.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    unmapAllMonitors();
    raiseNotification("[split-monitor-workspaces] Unloaded successfully!");
}
