#pragma once

#include <optional>
#include <string_view>

enum class GraphicsDeviceProfile {
    Desktop,
    Console,
    Mobile,
};

inline constexpr const char* GraphicsDeviceProfileName(GraphicsDeviceProfile profile) {
    switch (profile) {
    case GraphicsDeviceProfile::Console:
        return "console";
    case GraphicsDeviceProfile::Mobile:
        return "mobile";
    default:
        return "desktop";
    }
}

inline constexpr const char* GraphicsDeviceProfileLabel(GraphicsDeviceProfile profile) {
    switch (profile) {
    case GraphicsDeviceProfile::Console:
        return "Console (Modern Preferred)";
    case GraphicsDeviceProfile::Mobile:
        return "Mobile (Classic Deferred)";
    default:
        return "Desktop (Automatic)";
    }
}

inline std::optional<GraphicsDeviceProfile> ParseGraphicsDeviceProfile(std::string_view value) {
    if (value == "desktop")
        return GraphicsDeviceProfile::Desktop;
    if (value == "console")
        return GraphicsDeviceProfile::Console;
    if (value == "mobile")
        return GraphicsDeviceProfile::Mobile;
    return std::nullopt;
}
