
#pragma once

#include <hyprland/src/Compositor.hpp>
#include <string>

static std::string getMonitorIdentifier(const PHLMONITOR& monitor) {
    // this is a bit of a hack, but it's the best we can do to get a unique identifier for a monitor
    // that is stable across reloads and monitor disconnections
    return std::string(monitor->m_name) + "@" + std::to_string(monitor->m_pixelSize.x) + "x" + std::to_string(monitor->m_pixelSize.y);
}
