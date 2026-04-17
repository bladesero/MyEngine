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
    o.pos   = mul(float4(v.pos, 1.0f), g_MVP);
    o.color = v.color;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    return p.color;
}
