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

static HOOK_CALLBACK_FN* e_monitorAddedHandle = nullptr;
static HOOK_CALLBACK_FN* e_monitorRemovedHandle = nullptr;

const std::string getWorkspaceFromMonitor(CMonitor* monitor, const std::string& workspace)
{
    int workspaceIndex = 1;
    try {
        workspaceIndex = std::stoi(workspace);
    } catch (...) {
        return workspace;
    }

    // Checks
    int workspaceCount = g_pConfigManager->getConfigValuePtrSafe(k_workspaceCount)->intValue;
    if (workspaceIndex < 1 || workspaceIndex > workspaceCount) {
        return workspace;
    }

    // Compute workspace index
    std::size_t monitors = g_pCompositor->m_vMonitors.size();

    int actualIndex = (workspaceIndex - 1) * monitors + monitor->ID + 1;

    return std::to_string(actualIndex);
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
    std::size_t monitors = g_pCompositor->m_vMonitors.size();
    for (auto& workspace : g_pCompositor->m_vWorkspaces) {
        int workIndex = 0;
        try {
            workIndex = std::stoi(workspace->m_szName);
        } catch (...) {
            continue;
        }

        // Compute correct monitor (Reverse computation)
        int monitor = ((workIndex % monitors) + monitors - 1) % monitors;

        std::string cmd = "moveworkspacetomonitor "
            + std::to_string(workIndex) + " "
            + std::to_string(monitor);
        
        HyprlandAPI::addNotification(PHANDLE, cmd, s_pluginColor, 5000);
        HyprlandAPI::invokeHyprctlCommand("dispatch", cmd);
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
}
