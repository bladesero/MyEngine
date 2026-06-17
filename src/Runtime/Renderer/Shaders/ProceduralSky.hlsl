cbuffer SkyConstants : register(b0)
{
    float4 g_Forward;
    float4 g_Right;
    float4 g_Up;
    float4 g_Parameters;
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
    output.position = float4(output.ndc, 0.999f, 1.0f);
    return output;
}





float4 PSMain(VSOut input) : SV_TARGET
{
    float2 rayOffset = float2(
        input.ndc.x * g_Parameters.x * g_Parameters.y,
        input.ndc.y * g_Parameters.x);
    float3 direction = normalize(
        g_Forward.xyz + g_Right.xyz * rayOffset.x + g_Up.xyz * rayOffset.y);
    float3 hdrColor = EnvironmentRadiance(direction) * g_Parameters.z;
    float3 color = AcesToneMap(hdrColor * g_Parameters.w);
    return float4(pow(color, 1.0f / 2.2f), 1.0f);
}
