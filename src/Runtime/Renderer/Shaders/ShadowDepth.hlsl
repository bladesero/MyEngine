cbuffer ShadowPerDraw : register(b0)
{
    row_major float4x4 g_LightMVP;
};

struct VSIn
{
    float3 pos : POSITION;
};

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0f), g_LightMVP);
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return 1.0f.xxxx;
}
