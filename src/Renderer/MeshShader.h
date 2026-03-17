#pragma once

#include "IRenderContext.h"
#include "Assets/MeshAsset.h"

// Inline HLSL for mesh rendering: MeshVertex (POSITION, NORMAL, TANGENT, TEXCOORD0).
// VS constant buffer: MVP (64 bytes) + BaseColor (16 bytes, aligned).

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

// Layout matching MeshVertex (position, normal, tangent, u, v).
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
