cbuffer AtmosphereFaceConstants : register(b0)
{
    float4 g_FaceInfo;
};

#include "EnvironmentRadiance.hlsli"

struct VSOut
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    VSOut output;
    output.ndc = positions[vertexId];
    output.position = float4(output.ndc, 0.0f, 1.0f);
    return output;
}

float3 DirectionFromCubeFace(uint face, float2 uv)
{
    float2 p = uv * 2.0f - 1.0f;
    if (face == 0) return normalize(float3( 1.0f, p.y, -p.x));
    if (face == 1) return normalize(float3(-1.0f, p.y,  p.x));
    if (face == 2) return normalize(float3( p.x,  1.0f, -p.y));
    if (face == 3) return normalize(float3( p.x, -1.0f, p.y));
    if (face == 4) return normalize(float3( p.x, p.y,  1.0f));
    return normalize(float3(-p.x, p.y, -1.0f));
}



float4 PSMain(VSOut input) : SV_TARGET
{
    float2 uv = input.ndc * 0.5f + 0.5f;
    uint face = (uint)g_FaceInfo.x;
    float3 direction = DirectionFromCubeFace(face, uv);
    return float4(EnvironmentRadiance(direction), 1.0f);
}
