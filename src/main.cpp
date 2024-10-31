#include <cstddef>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include "globals.hpp"

#include <map>
#include <unistd.h>
#include <vector>

auto constexpr k_workspaceCount = "plugin:split-monitor-workspaces:count";
auto constexpr k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
auto constexpr k_enableNotifications = "plugin:split-monitor-workspaces:enable_notifications";
auto constexpr k_enablePersistentWorkspaces = "plugin:split-monitor-workspaces:enable_persistent_workspaces";

const CColor s_pluginColor = {0x61 / 255.0F, 0xAF / 255.0F, 0xEF / 255.0F, 1.0F};
bool g_enableNotifications = false;
bool g_enablePersistentWorkspaces = true;
bool g_keepFocused = false;
int g_workspaceCount;

// the first time we load the plugin, we want to switch to the first workspace on the first monitor regardless of keepFocused
bool g_firstLoad = true;

std::map<uint64_t, std::vector<std::string>> g_vMonitorWorkspaceMap;

static SP<HOOK_CALLBACK_FN> e_monitorAddedHandle = nullptr;
static SP<HOOK_CALLBACK_FN> e_monitorRemovedHandle = nullptr;
static SP<HOOK_CALLBACK_FN> e_configReloadedHandle = nullptr;

void raiseNotification(const std::string& message, float timeout = 5000.0F)
{
    if (g_enableNotifications) {
        HyprlandAPI::addNotification(PHANDLE, message, s_pluginColor, timeout);
    }
}

int getDelta(const std::string& direction)
{
    if (direction == "next" || direction == "+1" || direction == "1") {
        return 1;
    }
    if (direction == "prev" || direction == "-1") {
        return -1;
    }
    // fallback if input is incorrect
    return 0;
}

int getParamValue(const char* paramName)
{
    Debug::log(INFO, "[split-monitor-workspaces] Getting config value {}", paramName);
    const auto* const paramPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, paramName)->getDataStaticPtr();
    if (paramPtr == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to get config value {}", paramName);
        return 0;
    }
    return **paramPtr;
}

const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace)
{
    // if the workspace is "empty", we expect the new ID to be the first available ID on the given monitor (not the first ID in the global list)
    if (workspace == "empty") {
        // get the next workspace ID that is empty on this monitor
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitor->ID]) {
            PHLWORKSPACE workspacePtr = g_pCompositor->getWorkspaceByName(workspaceName);
            // the workspace we want is either not yet created (=nullptr) or already created but empty (!= nullptr but no windows)
            if (workspacePtr == nullptr || g_pCompositor->getWindowsOnWorkspace(workspacePtr->m_iID) == 0) {
                return workspaceName;
            }
        }
        // if not yet returned, we just return the last workspace in the map
        return g_vMonitorWorkspaceMap[monitor->ID].back();
    }

    // otherwise, try to parse the workspace as an integer
    int workspaceIndex = 0;
    try {
        // convert to 0-indexed int
        workspaceIndex = std::stoi(workspace) - 1;
    }
    catch (std::invalid_argument&) {
        // if parsing fails, assume the user wants to switch to the workspace by name
        Debug::log(WARN, "[split-monitor-workspaces] Invalid workspace index: {}, assuming named workspace", workspace.c_str());
        return workspace;
    }

    if (workspaceIndex < 0) {
        return workspace;
    }

    if ((size_t)workspaceIndex >= g_vMonitorWorkspaceMap[monitor->ID].size()) {
        return workspace;
    }

    return g_vMonitorWorkspaceMap[monitor->ID][workspaceIndex];
}

PHLMONITOR getCurrentMonitor()
{
    // get last focused monitor, because some people switch monitors with a keybind while the cursor is on a different monitor
    if (PHLMONITOR monitor = g_pCompositor->m_pLastMonitor.lock()) {
        return monitor;
    }
    Debug::log(WARN, "[split-monitor-workspaces] Last monitor does not exist, falling back to cursor's monitor");
    // fallback to the monitor the cursor is on
    return g_pCompositor->getMonitorFromCursor();
}

void splitWorkspace(const std::string& workspace)
{
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
}

void splitCycleWorkspaces(const std::string& value)
{
    int const delta = getDelta(value);
    if (delta == 0) {
        Debug::log(WARN, "[split-monitor-workspaces] Invalid cycle value: {}", value.c_str());
        return;
    }
    PHLMONITOR const monitor = getCurrentMonitor();
    auto const workspaces = g_vMonitorWorkspaceMap[monitor->ID];
    int index = -1;
    for (int i = 0; i < g_workspaceCount; i++) {
        if (workspaces[i] == monitor->activeWorkspace->m_szName) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        Debug::log(WARN, "[split-monitor-workspaces] Could not find active workspace in monitor workspaces. Aborting cycle.");
        return;
    }

    index += delta;
    if (index < 0)
        index = g_workspaceCount - 1;
    else if (index >= g_workspaceCount)
        index = 0;

    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + workspaces[index]);
}

void splitMoveToWorkspace(const std::string& workspace)
{
    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
}

void splitMoveToWorkspaceSilent(const std::string& workspace)
{
    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
}

void changeMonitor(bool quiet, const std::string& value)
{
    PHLMONITOR monitor = getCurrentMonitor();

    PHLMONITOR nextMonitor = nullptr;

    uint64_t monitorCount = g_pCompositor->m_vMonitors.size();

    int const delta = getDelta(value);
    if (delta == 0) {
        Debug::log(WARN, "[split-monitor-workspaces] Invalid monitor value: {}", value.c_str());
        return;
    }

    // The index is used instead of the monitorID because using the monitorID won't work if monitors are removed or mirrored
    // as there would be gaps in the monitorID sequence
    int currentMonitorIndex = -1;
    for (size_t i = 0; i < g_pCompositor->m_vMonitors.size(); i++) {
        if (g_pCompositor->m_vMonitors[i] == monitor) {
            currentMonitorIndex = i;
            break;
        }
    }
    if (currentMonitorIndex == -1) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor ID {} not found in monitor list?", monitor->ID);
        return;
    }

    int nextMonitorIndex = (monitorCount + currentMonitorIndex + delta) % monitorCount;

    nextMonitor = g_pCompositor->m_vMonitors[nextMonitorIndex];

    int nextWorkspaceID = nextMonitor->activeWorkspace->m_iID;

    if (quiet) {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + std::to_string(nextWorkspaceID));
    }
    else {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + std::to_string(nextWorkspaceID));
    }
}

void splitChangeMonitorSilent(const std::string& value)
{
    changeMonitor(true, value);
}

void splitChangeMonitor(const std::string& value)
{
    changeMonitor(false, value);
}

void mapMonitor(const PHLMONITOR& monitor) // NOLINT(readability-convert-member-functions-to-static)
{
    if (monitor->activeMonitorRule.disabled) {
        Debug::log(INFO, "[split-monitor-workspaces] Skipping disabled monitor {}", monitor->szName);
        return;
    }

    if (monitor->isMirror()) {
        Debug::log(INFO, "[split-monitor-workspaces] Skipping mirrored monitor {}", monitor->szName);
        return;
    }

    int workspaceIndex = monitor->ID * g_workspaceCount + 1;

    Debug::log(INFO, "{}",
               "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + g_workspaceCount - 1) + " to monitor " + monitor->szName);

    for (int i = workspaceIndex; i < workspaceIndex + g_workspaceCount; i++) {
        std::string workspaceName = std::to_string(i);
        g_vMonitorWorkspaceMap[monitor->ID].push_back(workspaceName);
        PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

        // when not using persistent workspaces, we still want to create the first workspace on each monitor
        // to avoid issues where only the last mapped monitor has the correct workspace (#121)
        if (workspace.get() == nullptr && (g_enablePersistentWorkspaces || i == workspaceIndex)) {
            Debug::log(INFO, "[split-monitor-workspaces] Creating workspace {}", workspaceName);
            workspace = g_pCompositor->createNewWorkspace(i, monitor->ID);
        }
        if (workspace.get() != nullptr) {
            Debug::log(INFO, "[split-monitor-workspaces] Moving workspace {} to monitor {}", workspaceName, monitor->szName);
            g_pCompositor->moveWorkspaceToMonitor(workspace, monitor);
            if (g_enablePersistentWorkspaces) {
                workspace->m_bPersistent = true;
            }
        }
    }

    if (!g_keepFocused || g_firstLoad) {
        // we also want to switch to the first workspace when the plugin is first loaded
        Debug::log(INFO, "[split-monitor-workspaces] Switching to first workspace on monitor {}", monitor->szName);
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(workspaceIndex));
    }
}

void unmapMonitor(const PHLMONITOR& monitor)
{
    int workspaceIndex = monitor->ID * g_workspaceCount + 1;

    Debug::log(INFO, "{}",
               "[split-monitor-workspaces] Unmapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + g_workspaceCount - 1) + " from monitor " + monitor->szName);

    if (g_vMonitorWorkspaceMap.contains(monitor->ID)) {
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitor->ID]) {
            PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace.get() != nullptr) {
                workspace->m_bPersistent = false;
            }
        }
        g_vMonitorWorkspaceMap.erase(monitor->ID);
    }
}

void unmapAllMonitors()
{
    while (!g_vMonitorWorkspaceMap.empty()) {
        auto [monitorID, workspaces] = *g_vMonitorWorkspaceMap.begin();
        PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitorID);
        if (monitor != nullptr) {
            unmapMonitor(monitor); // will remove the monitor from the map
        }
        else {
            g_vMonitorWorkspaceMap.erase(monitorID); // remove it manually
        }
    }
    g_vMonitorWorkspaceMap.clear();
}

void remapAllMonitors()
{
    raiseNotification("[split-monitor-workspaces] Remapping workspaces...");
    unmapAllMonitors();
    for (const PHLMONITOR& monitor : g_pCompositor->m_vMonitors) {
        mapMonitor(monitor);
    }
}

void loadConfigValues()
{
    g_enableNotifications = getParamValue(k_enableNotifications) != 0;
    g_enablePersistentWorkspaces = getParamValue(k_enablePersistentWorkspaces) != 0;
    g_keepFocused = getParamValue(k_keepFocused) != 0;
    g_workspaceCount = getParamValue(k_workspaceCount);
}

void reload()
{
    loadConfigValues();
    remapAllMonitors();
    g_firstLoad = false;
}

void monitorAddedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any param)
{ // NOLINT(performance-unnecessary-value-param)
    auto monitor = std::any_cast<PHLMONITOR>(param);
    if (monitor == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor added callback called with nullptr?");
        return;
    }
    mapMonitor(monitor);
}

void monitorRemovedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any param) // NOLINT(performance-unnecessary-value-param)
{
    auto monitor = std::any_cast<PHLMONITOR>(param);
    if (monitor == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor removed callback called with nullptr?");
        return;
    }
    unmapMonitor(monitor);
}

void configReloadedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any /*unused*/) // NOLINT(performance-unnecessary-value-param)
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

    HyprlandAPI::addDispatcher(PHANDLE, "split-workspace", splitWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-cycleworkspaces", splitCycleWorkspaces);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitor", splitChangeMonitor);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);

    // reload the config before adding the callback, so we can already use the config's values we defined above
    HyprlandAPI::reloadConfig();
    g_pConfigManager->tick();
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
