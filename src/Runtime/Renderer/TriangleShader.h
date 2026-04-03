#pragma once

#include "Core/Platform.h"

// ============================================================================
// Triangle shader sources – HLSL (Windows / D3D) and MSL (macOS / Metal).
//
// Both shaders implement identical semantics:
//   VS constant buffer slot / buffer index 1: MVP matrix (64 bytes)
//   Vertex attributes: POSITION (float3) + COLOR (float4)
// ============================================================================

// ---------------------------------------------------------------------------
// HLSL – DirectX 11 / 12 (Windows)
// ---------------------------------------------------------------------------
inline constexpr const char* k_TriangleHLSL = R"HLSL(

cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_MVP;
};

struct VSIn
{
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct VSOut
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    // C++ uploads row-major MVP; HLSL reads as transpose. Use row-vector * matrix.
    o.pos   = mul(float4(v.pos, 1.0f), g_MVP);
    o.color = v.color;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    return p.color;
}

)HLSL";

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

// Platform-selected shader source.
#ifdef MYENGINE_PLATFORM_MACOS
inline constexpr const char* k_TriangleShaderSource = k_TriangleMSL;
#else
inline constexpr const char* k_TriangleShaderSource = k_TriangleHLSL;
#endif
