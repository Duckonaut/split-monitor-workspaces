#include <cstddef>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Workspace.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include "globals.hpp"
#include "hyprland/src/SharedDefs.hpp"

#include <algorithm>
#include <map>
#include <thread>
#include <unistd.h>
#include <vector>

const std::string k_workspaceCount = "plugin:split-monitor-workspaces:count";
const std::string k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
const CColor s_pluginColor = {0x61 / 255.0f, 0xAF / 255.0f, 0xEF / 255.0f, 1.0f};

std::map<uint64_t, std::vector<std::string>> g_vMonitorWorkspaceMap;

static HOOK_CALLBACK_FN* e_monitorAddedHandle = nullptr;
static HOOK_CALLBACK_FN* e_monitorRemovedHandle = nullptr;
static HOOK_CALLBACK_FN* e_configReloadedHandle = nullptr;

const std::string& getWorkspaceFromMonitor(CMonitor* monitor, const std::string& workspace)
{
    int workspaceIndex = 0;
    try {
        // convert to 0-indexed int
        workspaceIndex = std::stoi(workspace) - 1;
    }
    catch (std::invalid_argument&) {
        Debug::log(WARN, "[split-monitor-workspaces] Invalid workspace index: %s", workspace.c_str());
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

void splitWorkspace(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + getWorkspaceFromMonitor(monitor, workspace));
}

void splitMoveToWorkspace(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + getWorkspaceFromMonitor(monitor, workspace));
}

void splitMoveToWorkspaceSilent(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + getWorkspaceFromMonitor(monitor, workspace));
}

void changeMonitor(bool quiet, std::string value)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

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
        Debug::log(WARN, "[split-monitor-workspaces] Invalid monitor value: %s", value.c_str());
        return;
    }

    int nextMonitorIndex = (monitor->ID + delta) % monitorCount;

    if (nextMonitorIndex < 0) {
        nextMonitorIndex += monitorCount;
    }

    nextMonitor = g_pCompositor->m_vMonitors[nextMonitorIndex].get();

    int nextWorkspace = nextMonitor->activeWorkspace;

    if (quiet) {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + std::to_string(nextWorkspace));
    }
    else {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + std::to_string(nextWorkspace));
    }
}

void splitChangeMonitorSilent(std::string value)
{
    changeMonitor(true, value);
}

void splitChangeMonitor(std::string value)
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
            file << std::endl;
            file.close();
            return;
        }
        for (const auto& rule : rules) {
            file << rule << std::endl;
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
    for (auto& monitor : g_vMonitorWorkspaceMap) {
        for (auto& workspace : monitor.second) {
            CWorkspace* workspacePtr = g_pCompositor->getWorkspaceByName(workspace);
            if (workspacePtr != nullptr) {
                g_pCompositor->moveWorkspaceToMonitor(workspacePtr, g_pCompositor->getMonitorFromID(monitor.first));
            }
        }
    }
}

void mapWorkspacesToMonitors()
{
    g_vMonitorWorkspaceMap.clear();

    static auto* const workspaceCountPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, k_workspaceCount)->getDataStaticPtr();
    static auto* const keepFocusedPtr = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, k_keepFocused)->getDataStaticPtr();

    int keepFocused = **keepFocusedPtr;
    int workspaceCount = **workspaceCountPtr;

    Debug::log(INFO, "[split-monitor-workspaces] Mapping {} workspaces to monitors...", workspaceCount);

    std::vector<std::string> workspaceRules;
    writeWorkspaceRules(workspaceRules); // clear the file first
    for (auto& monitor : g_pCompositor->m_vMonitors) {
        int workspaceIndex = monitor->ID * workspaceCount + 1;
        std::string logMessage =
            "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + workspaceCount - 1) + " to monitor " + monitor->szName;

        HyprlandAPI::addNotification(PHANDLE, logMessage, s_pluginColor, 5000);

        Debug::log(INFO, "{}", logMessage);
        for (int i = workspaceIndex; i < workspaceIndex + workspaceCount; i++) {
            std::string workspaceName = std::to_string(i);
            g_vMonitorWorkspaceMap[monitor->ID].push_back(workspaceName);
            HyprlandAPI::invokeHyprctlCommand("keyword", "workspace " + workspaceName + "," + monitor->szName);
            CWorkspace* workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace != nullptr) {
                g_pCompositor->moveWorkspaceToMonitor(workspace, monitor.get());
            }

            // The plugin can't persistently set the workspace rules, so we have to also write them to a file manually
            workspaceRules.emplace_back("workspace = " + workspaceName + ",monitor:" + monitor->szName + ",persistent:true");
        }

        if (!keepFocused) {
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(workspaceIndex));
        }
    }
    writeWorkspaceRules(workspaceRules);
    HyprlandAPI::reloadConfig();
}

void refreshMapping(void*, SCallbackInfo&, std::any)
{
    mapWorkspacesToMonitors();
}

void configReloadedCallback(void*, SCallbackInfo&, std::any)
{
    // anything you call in this function should not reload the config, as it will cause an infinite loop
    Debug::log(INFO, "[split-monitor-workspaces] Config reloaded");
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

    HyprlandAPI::addDispatcher(PHANDLE, "split-workspace", splitWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitor", splitChangeMonitor);
    HyprlandAPI::addDispatcher(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);

    HyprlandAPI::reloadConfig();
    g_pConfigManager->tick();

    mapWorkspacesToMonitors();

    HyprlandAPI::addNotification(PHANDLE, "[split-monitor-workspaces] Initialized successfully!", s_pluginColor, 5000);

    e_monitorAddedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", refreshMapping);
    e_monitorRemovedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", refreshMapping);
    e_configReloadedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", configReloadedCallback);

    return {"split-monitor-workspaces", "Split monitor workspace namespaces", "Duckonaut", "1.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    writeWorkspaceRules({});
    HyprlandAPI::addNotification(PHANDLE, "[split-monitor-workspaces] Unloaded successfully!", s_pluginColor, 5000);

    g_vMonitorWorkspaceMap.clear();
}
