#pragma once

#include "Core/Platform.h"

// ============================================================================
// Triangle: Windows uses offline DXBC from Shaders/Triangle.hlsl (dxc).
// macOS uses MSL below.
//
// VS constant buffer slot / buffer index 1: MVP matrix (64 bytes)
// Vertex attributes: POSITION (float3) + COLOR (float4)
// ============================================================================

// ---------------------------------------------------------------------------
// MSL – Metal (macOS)
// Constants at buffer index 1; vertex buffer at index 0.
// ---------------------------------------------------------------------------
inline constexpr const char* k_TriangleMSL = R"MSL(

#include <metal_stdlib>
using namespace metal;

struct VSIn {
    float3 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

struct VSOut {
    float4 position [[position]];
    float4 color;
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant float4x4& g_MVP [[buffer(1)]])
{
    VSOut out;
    out.position = g_MVP * float4(in.position, 1.0);
    out.color    = in.color;
    return out;
}

fragment float4 PSMain(VSOut in [[stage_in]])
{
    return in.color;
}

)MSL";

#ifdef MYENGINE_PLATFORM_MACOS
inline constexpr const char* k_TriangleShaderSource = k_TriangleMSL;
#elif !defined(MYENGINE_PLATFORM_WINDOWS)
inline constexpr const char* k_TriangleShaderSource = "";
#endif
