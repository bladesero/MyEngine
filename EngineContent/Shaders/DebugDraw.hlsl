cbuffer DebugDrawConstants : register(b0)
{
    row_major float4x4 g_ViewProjection;
    row_major float4x4 g_World[64];
    float4 g_Color[64];
    float4 g_ShapeParameters[64];
};

struct VSInput
{
    float3 position : POSITION;
    float2 auxiliary : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VSOutput VSMain(VSInput input, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    float3 localPosition = input.position;
    const float4 shape = g_ShapeParameters[instanceId];
    if (shape.w > 0.5f)
    {
        localPosition.xz *= shape.x;
        localPosition.y = (localPosition.y - input.auxiliary.x) * shape.x + input.auxiliary.x * shape.y;
    }
    const float4 worldPosition = mul(float4(localPosition, 1.0f), g_World[instanceId]);
    output.position = mul(worldPosition, g_ViewProjection);
    output.color = g_Color[instanceId];
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
