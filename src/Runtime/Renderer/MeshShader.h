#pragma once

#include "Core/Platform.h"
#include "Renderer/RHI/VertexLayout.h"
#include "Assets/MeshAsset.h"

// ============================================================================
// Legacy Metal source. Windows uses Content/Engine/Shaders/Mesh.shader.
// (built with dxc via xmake). macOS uses MSL below. Linux has no GPU backend yet.
//
// Semantics:
//   VS constant buffer slot / buffer index 1: MVP (64 bytes) + BaseColor (16 bytes)
//   Vertex attributes: POSITION (float3), NORMAL (float3), TANGENT (float3), UV (float2)
// ============================================================================

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
    float3 tangent;
    float2 uv;
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant PerDraw& PerDraw [[buffer(1)]])
{
    VSOut out;
    out.position = PerDraw.g_MVP * float4(in.position, 1.0);
    out.normal   = in.normal;
    out.tangent  = in.tangent;
    out.uv       = in.uv;
    return out;
}

fragment float4 PSMain(VSOut in [[stage_in]],
                        constant PerDraw& PerDraw [[buffer(1)]])
{
    float d = saturate(dot(in.normal, normalize(float3(0.0, 1.0, 1.0))));
    d += dot(in.tangent, float3(1.0, 1.0, 1.0)) * 1e-10f;
    return 0.5 + 0.5 * PerDraw.g_BaseColor * d;
}

)MSL";

// Runtime MSL source retained for the Metal backend.
#ifdef MYENGINE_PLATFORM_MACOS
inline constexpr const char* k_MeshShaderSource = k_MeshMSL;
#elif !defined(MYENGINE_PLATFORM_WINDOWS)
inline constexpr const char* k_MeshShaderSource = "";
#endif

// ---------------------------------------------------------------------------
// Vertex layout – semantic names match HLSL; attribute indices match MSL.
// ---------------------------------------------------------------------------
inline const VertexElement k_MeshVertexLayout[] = {
    { "POSITION", 0, VertexFormat::Float3, offsetof(MeshVertex, position) },
    { "NORMAL",   0, VertexFormat::Float3, offsetof(MeshVertex, normal)   },
    { "TANGENT",  0, VertexFormat::Float3, offsetof(MeshVertex, tangent)  },
    { "TEXCOORD", 0, VertexFormat::Float2, offsetof(MeshVertex, u)       },
    { "BLENDINDICES", 0, VertexFormat::Float4, offsetof(MeshVertex, boneIndices) },
    { "BLENDWEIGHT",  0, VertexFormat::Float4, offsetof(MeshVertex, boneWeights) },
};
inline constexpr uint32_t k_MeshVertexLayoutCount = 6;

// Per-draw constants: Mat4 MVP + Vec4 BaseColor (80 bytes, 16-byte aligned).
struct MeshPerDrawConstants {
    float mvp[16];
    float baseColor[4];
};
