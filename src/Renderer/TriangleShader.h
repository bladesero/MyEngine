#pragma once

// Inline HLSL – avoids file I/O at runtime.
// Vertex input: POSITION (float3) + COLOR (float4).
// VS constant buffer slot 0 carries a 4x4 MVP matrix (column-major).

inline constexpr const char* k_TriangleHLSL = R"HLSL(

cbuffer PerDraw : register(b0)
{
    float4x4 g_MVP;
};

struct VSIn
{
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct PSIn
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
};

PSIn VSMain(VSIn v)
{
    PSIn o;
    o.pos   = mul(float4(v.pos, 1.0f), g_MVP);
    o.color = v.color;
    return o;
}

float4 PSMain(PSIn p) : SV_TARGET
{
    return p.color;
}

)HLSL";
