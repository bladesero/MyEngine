#pragma once

#include <array>
#include <string_view>

namespace PublishTargets {
struct TargetInfo {
    const char* id;
    const char* label;
};

inline constexpr TargetInfo kWindowsX64{"windows-x64", "Windows x64"};
inline constexpr std::array<TargetInfo, 1> kSupported{{kWindowsX64}};
inline constexpr const char* kDefaultTargetId = kWindowsX64.id;
inline constexpr const char* kDefaultTargetLabel = kWindowsX64.label;

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
