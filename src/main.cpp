#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
const CHyprColor s_pluginColor = {0xFF / 255.0F, 0x00, 0x00, 1.0F};

HANDLE PHANDLE = nullptr;
CHyprSignalListener e_configReloadedHandle = nullptr;

void raiseNotification(const std::string& message, float timeout = 5000.0F)
{
    HyprlandAPI::addNotification(PHANDLE, message, s_pluginColor, timeout);
}

} // namespace

void configReloadedCallback()
{
    // remind the user every time the config reloads
    raiseNotification("[split-monitor-workspaces] The C++ plugin has been deprecated, please use the Lua package instead. See the readme for details.", 10000.0F);
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;
    e_configReloadedHandle = Event::bus()->m_events.config.reloaded.listen(configReloadedCallback);

    Log::logger->log(Log::ERR, "[split-monitor-workspaces] The C++ plugin has been deprecated, please use the Lua package instead. See the readme for details.");
    return {.name = "split-monitor-workspaces", .description = "Split monitor workspace namespaces", .author = "zjeffer", .version = "1.3.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {}
