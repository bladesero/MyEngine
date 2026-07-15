#pragma once

#include <cstdint>

enum class RendererFeatureMask : uint32_t {
    None = 0,
    Shadows = 1u << 0,
    SSAO = 1u << 1,
    ScreenUI = 1u << 2,
    All = (1u << 0) | (1u << 1) | (1u << 2),
};

constexpr RendererFeatureMask operator|(RendererFeatureMask lhs, RendererFeatureMask rhs) {
    return static_cast<RendererFeatureMask>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr RendererFeatureMask operator&(RendererFeatureMask lhs, RendererFeatureMask rhs) {
    return static_cast<RendererFeatureMask>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr RendererFeatureMask operator~(RendererFeatureMask value) {
    return static_cast<RendererFeatureMask>(~static_cast<uint32_t>(value));
}

constexpr RendererFeatureMask& operator|=(RendererFeatureMask& lhs, RendererFeatureMask rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr RendererFeatureMask& operator&=(RendererFeatureMask& lhs, RendererFeatureMask rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr bool HasRendererFeature(RendererFeatureMask mask, RendererFeatureMask feature) {
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(feature)) == static_cast<uint32_t>(feature);
}
