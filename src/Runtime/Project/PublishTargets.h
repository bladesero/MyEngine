#pragma once

#include <array>
#include <string_view>

namespace PublishTargets {
struct TargetInfo {
    const char* id;
    const char* label;
};

inline constexpr TargetInfo kWindowsX64{"windows-x64", "Windows x64"};
inline constexpr TargetInfo kMacOSArm64{"macos-arm64", "macOS arm64"};
inline constexpr std::array<TargetInfo, 2> kSupported{{kWindowsX64, kMacOSArm64}};
#if defined(MYENGINE_PLATFORM_MACOS)
inline constexpr const char* kDefaultTargetId = kMacOSArm64.id;
inline constexpr const char* kDefaultTargetLabel = kMacOSArm64.label;
#else
inline constexpr const char* kDefaultTargetId = kWindowsX64.id;
inline constexpr const char* kDefaultTargetLabel = kWindowsX64.label;
#endif

inline constexpr bool IsSupported(std::string_view target) {
    for (const TargetInfo& info : kSupported) {
        if (target == info.id) return true;
    }
    return false;
}

inline constexpr const TargetInfo* Find(std::string_view target) {
    for (const TargetInfo& info : kSupported) {
        if (target == info.id) return &info;
    }
    return nullptr;
}
}
