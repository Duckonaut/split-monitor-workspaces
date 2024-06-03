#include <cstddef>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/memory/SharedPtr.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "globals.hpp"
#include "hyprland/src/SharedDefs.hpp"

#include <map>
#include <unistd.h>
#include <vector>

auto constexpr k_workspaceCount = "plugin:split-monitor-workspaces:count";
auto constexpr k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
auto constexpr k_enableNotifications = "plugin:split-monitor-workspaces:enable_notifications";

const CColor s_pluginColor = {0x61 / 255.0F, 0xAF / 255.0F, 0xEF / 255.0F, 1.0F};
bool g_enableNotifications = false;

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

bool getIsNotificationsEnabled()
{
    static const auto* const enableNotificationsPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, k_enableNotifications)->getDataStaticPtr();
    if (enableNotificationsPtr == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to get enable notifications config value");
        return false;
    }
    return **enableNotificationsPtr != 0;
}

const std::string& getWorkspaceFromMonitor(CMonitor* monitor, const std::string& workspace)
{
    // if the workspace is "empty", we expect the new ID to be the next available ID on the given monitor (not the next ID in the global list)
    if (workspace == "empty") {
        // get the next workspace ID that is empty on this monitor
        PHLWORKSPACE activeWorkspace = monitor->activeWorkspace;
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitor->ID]) {
            PHLWORKSPACE workspacePtr = g_pCompositor->getWorkspaceByName(workspaceName);
            if (workspacePtr == activeWorkspace) {
                // skip the currently active workspace
                continue;
            }
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

CMonitor* getCurrentMonitor()
{
    // get last focused monitor, because some people switch monitors with a keybind while the cursor is on a different monitor
    if (CMonitor* monitor = g_pCompositor->m_pLastMonitor.lock().get()) {
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
    CMonitor* monitor = getCurrentMonitor();

    CMonitor* nextMonitor = nullptr;

    uint64_t monitorCount = g_pCompositor->m_vMonitors.size();

    int delta = 0;

    if (value == "next" || value == "+1") {
        delta = 1;
    }
    else if (value == "prev" || value == "-1") {
        delta = -1;
    }
    else {
        Debug::log(WARN, "[split-monitor-workspaces] Invalid monitor value: {}", value.c_str());
        return;
    }

    // The index is used instead of the monitorID because using the monitorID won't work if monitors are removed or mirrored
    // as there would be gaps in the monitorID sequence
    int currentMonitorIndex = -1;
    for (size_t i = 0; i < g_pCompositor->m_vMonitors.size(); i++) {
        if (g_pCompositor->m_vMonitors[i].get() == monitor) {
            currentMonitorIndex = i;
            break;
        }
    }
    if (currentMonitorIndex == -1) {
        Debug::log(WARN, "[split-monitor-workspaces] Monitor ID {} not found in monitor list?", monitor->ID);
        return;
    }

    int nextMonitorIndex = (monitorCount + currentMonitorIndex + delta) % monitorCount;

    nextMonitor = g_pCompositor->m_vMonitors[nextMonitorIndex].get();

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

void writeWorkspaceRules(std::vector<std::string> const& rules)
{
    // Write the workspacerules to a file, source this same file in your hyprland config
    try {
        std::ofstream file;
        file.open("/tmp/hyprland-workspace-rules", std::ios::out | std::ios::trunc);
        if (rules.empty()) { // clear the file
            file << '\n';
            file.close();
            return;
        }
        for (const auto& rule : rules) {
            file << rule << '\n';
        }
        file.close();
    }
    catch (std::exception& e) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to write workspace rules: {}", e.what());
    }
}

void fixWorkspaceArrangement()
{
    // for all monitors in the map, move the workspaces to the correct monitor
    for (auto const& [monitorID, workspaces] : g_vMonitorWorkspaceMap) {
        auto* const monitorPtr = g_pCompositor->getMonitorFromID(monitorID);
        if (monitorPtr == nullptr) {
            Debug::log(WARN, "[split-monitor-workspaces] fixWorkspaceArrangement: Monitor not found: {}", monitorID);
            continue;
        }

        for (auto const& workspace : workspaces) {
            PHLWORKSPACE workspacePtr = g_pCompositor->getWorkspaceByName(workspace);
            if (workspacePtr != nullptr) {
                g_pCompositor->moveWorkspaceToMonitor(workspacePtr, monitorPtr);
            }
            else {
                Debug::log(WARN, "[split-monitor-workspaces] fixWorkspaceArrangement: Workspace not found: {}", workspace);
            }
        }
        // check if currently focused workspace on this monitor actually belongs to this monitor, if not, switch to the first workspace
        if (std::find(workspaces.begin(), workspaces.end(), monitorPtr->activeWorkspace->m_szName) == workspaces.end()) {
            if (!workspaces.empty()) {
                HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + workspaces[0]);
            }
        }
    }
}

void mapWorkspacesToMonitors()
{
    g_vMonitorWorkspaceMap.clear();

    static const auto* const workspaceCountPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, k_workspaceCount)->getDataStaticPtr();
    static const auto* const keepFocusedPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, k_keepFocused)->getDataStaticPtr();

    if (workspaceCountPtr == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to get workspace count config value");
        return;
    }
    if (keepFocusedPtr == nullptr) {
        Debug::log(WARN, "[split-monitor-workspaces] Failed to get keep focused config value");
        return;
    }

    int keepFocused = **keepFocusedPtr;
    int workspaceCount = **workspaceCountPtr;

    Debug::log(INFO, "[split-monitor-workspaces] Mapping {} workspaces to monitors...", workspaceCount);

    std::vector<std::string> workspaceRules;
    writeWorkspaceRules(workspaceRules); // clear the file first
    for (auto& monitor : g_pCompositor->m_vMonitors) {
        if (monitor->isMirror()) {
            Debug::log(INFO, "[split-monitor-workspaces] Skipping mirrored monitor {}", monitor->szName);
            continue;
        }

        int workspaceIndex = monitor->ID * workspaceCount + 1;

        std::string logMessage =
            "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + workspaceCount - 1) + " to monitor " + monitor->szName;
        raiseNotification(logMessage);
        Debug::log(INFO, "{}", logMessage);

        for (int i = workspaceIndex; i < workspaceIndex + workspaceCount; i++) {
            std::string workspaceName = std::to_string(i);
            g_vMonitorWorkspaceMap[monitor->ID].push_back(workspaceName);
            HyprlandAPI::invokeHyprctlCommand("keyword", "workspace " + workspaceName + "," + monitor->szName);
            PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace != nullptr) {
                g_pCompositor->moveWorkspaceToMonitor(workspace, monitor.get());
            }

            // The plugin can't persistently set the workspace rules, so we have to also write them to a file manually
            workspaceRules.emplace_back("workspace = " + workspaceName + ",monitor:" + monitor->szName + ",persistent:true");
        }

        if (keepFocused == 0) {
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(workspaceIndex));
        }
    }
    writeWorkspaceRules(workspaceRules);
    HyprlandAPI::reloadConfig();
}

void refreshMapping(void* /*unused*/, SCallbackInfo& /*unused*/, std::any /*unused*/) // NOLINT(performance-unnecessary-value-param)
{
    mapWorkspacesToMonitors();
}

void configReloadedCallback(void* /*unused*/, SCallbackInfo& /*unused*/, std::any /*unused*/) // NOLINT(performance-unnecessary-value-param)
{
    // anything you call in this function should not reload the config, as it will cause an infinite loop
    Debug::log(INFO, "[split-monitor-workspaces] Config reloaded");
    g_enableNotifications = getIsNotificationsEnabled();
    raiseNotification("[split-monitor-workspaces] Config reloaded");
    fixWorkspaceArrangement();
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

    HyprlandAPI::addDispatcher(PHANDLE, "split-workspace", splitWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitor", splitChangeMonitor);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);

    HyprlandAPI::reloadConfig();
    g_pConfigManager->tick();

    g_enableNotifications = getIsNotificationsEnabled();
    Debug::log(INFO, "[split-monitor-workspaces] Notifications are {}", g_enableNotifications ? "enabled" : "disabled");
    mapWorkspacesToMonitors();

    raiseNotification("[split-monitor-workspaces] Initialized successfully!");

    e_monitorAddedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", refreshMapping);
    e_monitorRemovedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", refreshMapping);
    e_configReloadedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", configReloadedCallback);

    return {"split-monitor-workspaces", "Split monitor workspace namespaces", "Duckonaut", "1.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    writeWorkspaceRules({});
    raiseNotification("[split-monitor-workspaces] Unloaded successfully!");

    g_vMonitorWorkspaceMap.clear();
}
