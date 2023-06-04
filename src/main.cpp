#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/helpers/Workspace.hpp>

#include "globals.hpp"

#include <algorithm>
#include <map>
#include <thread>
#include <unistd.h>
#include <vector>

const std::string k_workspaceCount = "plugin:split-monitor-workspaces:count";
const CColor s_pluginColor = {0x61 / 255.0f, 0xAF / 255.0f, 0xEF / 255.0f, 1.0f};

std::map<uint64_t, std::vector<std::string>> g_vMonitorWorkspaceMap;

static HOOK_CALLBACK_FN* e_monitorAddedHandle = nullptr;
static HOOK_CALLBACK_FN* e_monitorRemovedHandle = nullptr;

const std::string& getWorkspaceFromMonitor(CMonitor* monitor, const std::string& workspace)
{
    int workspaceIndex = std::stoi(workspace);
    if (workspaceIndex - 1 < 0) {
        return workspace;
    }

    if (workspaceIndex - 1 >= g_vMonitorWorkspaceMap[monitor->ID].size()) {
        return workspace;
    }

    return g_vMonitorWorkspaceMap[monitor->ID][workspaceIndex - 1];
}

void monitorWorkspace(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + getWorkspaceFromMonitor(monitor, workspace));
}

void monitorMoveToWorkspace(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace " + getWorkspaceFromMonitor(monitor, workspace));
}

void monitorMoveToWorkspaceSilent(std::string workspace)
{
    CMonitor* monitor = g_pCompositor->getMonitorFromCursor();

    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent " + getWorkspaceFromMonitor(monitor, workspace));
}

void mapWorkspacesToMonitors()
{
    g_vMonitorWorkspaceMap.clear();

    int workspaceIndex = 1;

    for (auto& monitor : g_pCompositor->m_vMonitors) {
        int workspaceCount = g_pConfigManager->getConfigValuePtrSafe(k_workspaceCount)->intValue;
        std::string logMessage =
            "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + workspaceCount - 1) + " to monitor " + monitor->szName;

        HyprlandAPI::addNotification(PHANDLE, logMessage, s_pluginColor, 5000);

        for (int i = workspaceIndex; i < workspaceIndex + workspaceCount; i++) {
            std::string workspaceName = std::to_string(i);
            g_vMonitorWorkspaceMap[monitor->ID].push_back(workspaceName);
            HyprlandAPI::invokeHyprctlCommand("keyword", "workspace " + workspaceName + "," + monitor->szName);
            CWorkspace* workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace != nullptr) {
                g_pCompositor->moveWorkspaceToMonitor(workspace, monitor.get());
            }
        }
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace " + std::to_string(workspaceIndex));
        workspaceIndex += workspaceCount;
    }
}

void refreshMapping(void*, std::any value)
{
    mapWorkspacesToMonitors();
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    HyprlandAPI::addConfigValue(PHANDLE, k_workspaceCount, SConfigValue{.intValue = 10});

    HyprlandAPI::addDispatcher(PHANDLE, "split-workspace", monitorWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspace", monitorMoveToWorkspace);
    HyprlandAPI::addDispatcher(PHANDLE, "split-movetoworkspacesilent", monitorMoveToWorkspaceSilent);

    HyprlandAPI::reloadConfig();

    mapWorkspacesToMonitors();

    HyprlandAPI::addNotification(PHANDLE, "[split-monitor-workspaces] Initialized successfully!", s_pluginColor, 5000);

    e_monitorAddedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorAdded", refreshMapping);
    e_monitorRemovedHandle = HyprlandAPI::registerCallbackDynamic(PHANDLE, "monitorRemoved", refreshMapping);

    return {"split-monitor-workspaces", "Split monitor workspace namespaces", "Duckonaut", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    HyprlandAPI::addNotification(PHANDLE, "[split-monitor-workspaces] Unloaded successfully!", s_pluginColor, 5000);

    g_vMonitorWorkspaceMap.clear();
}
