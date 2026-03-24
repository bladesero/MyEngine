#pragma once

#include "Core/Platform.h"
#include "IRenderContext.h"
#include "Assets/MeshAsset.h"

// ============================================================================
// Mesh shader sources – HLSL (Windows / D3D) and MSL (macOS / Metal).
//
// Both shaders implement identical semantics:
//   VS constant buffer slot / buffer index 1: MVP (64 bytes) + BaseColor (16 bytes)
//   Vertex attributes: POSITION (float3), NORMAL (float3), TANGENT (float3), UV (float2)
// ============================================================================

// ---------------------------------------------------------------------------
// HLSL – DirectX 11 / 12 (Windows)
// ---------------------------------------------------------------------------
inline constexpr const char* k_MeshHLSL = R"HLSL(

cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_MVP;
    float4   g_BaseColor;
};

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal   : NORMAL;
    float3 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct VSOut
{
    float4 pos    : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    // Row-major + row-vector convention (no transpose anywhere).
    o.pos   = mul(float4(v.pos, 1.0f), g_MVP);
    o.normal = v.normal;
    o.uv    = v.uv;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    return 0.5+0.5*g_BaseColor*saturate(dot(p.normal,normalize(float3(0.0f,1.0f,1.0f))));
}

)HLSL";

// ---------------------------------------------------------------------------
// MSL – Metal (macOS)
// The same row-major MVP stored in C++ maps correctly: `M * v` in MSL with
// column-major float4x4 reading the same memory is equivalent to HLSL's
// `mul(v, M)` with row_major storage (the indices permute identically).
// Constants at buffer index 1; vertex buffer at index 0.
// ---------------------------------------------------------------------------
inline constexpr const char* k_MeshMSL = R"MSL(

#include <metal_stdlib>
using namespace metal;

struct PerDraw {
    float4x4 g_MVP;
    float4   g_BaseColor;
};

struct VSIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float3 tangent  [[attribute(2)]];
    float2 uv       [[attribute(3)]];
};

struct VSOut {
    float4 position [[position]];
    float3 normal;
    float2 uv;
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant PerDraw& perDraw [[buffer(1)]])
{
    VSOut out;
    out.position = perDraw.g_MVP * float4(in.position, 1.0);
    out.normal   = in.normal;
    out.uv       = in.uv;
    return out;
}

fragment float4 PSMain(VSOut in [[stage_in]],
                        constant PerDraw& perDraw [[buffer(1)]])
{
    return 0.5 + 0.5 * perDraw.g_BaseColor *
           saturate(dot(in.normal, normalize(float3(0.0, 1.0, 1.0))));
}

)MSL";

// Platform-selected shader source used by Renderer.cpp / TriangleLayer.cpp.
#ifdef MYENGINE_PLATFORM_MACOS
inline constexpr const char* k_MeshShaderSource = k_MeshMSL;
#else
inline constexpr const char* k_MeshShaderSource = k_MeshHLSL;
#endif

// ---------------------------------------------------------------------------
// Vertex layout – semantic names match HLSL; attribute indices match MSL.
// ---------------------------------------------------------------------------
inline const VertexElement k_MeshVertexLayout[] = {
    { "POSITION", 0, VertexFormat::Float3, offsetof(MeshVertex, position) },
    { "NORMAL",   0, VertexFormat::Float3, offsetof(MeshVertex, normal)   },
    { "TANGENT",  0, VertexFormat::Float3, offsetof(MeshVertex, tangent)  },
    { "TEXCOORD", 0, VertexFormat::Float2, offsetof(MeshVertex, u)       },
};
inline constexpr uint32_t k_MeshVertexLayoutCount = 4;

// Per-draw constants: Mat4 MVP + Vec4 BaseColor (80 bytes, 16-byte aligned).
struct MeshPerDrawConstants {
    float mvp[16];
    float baseColor[4];
};
