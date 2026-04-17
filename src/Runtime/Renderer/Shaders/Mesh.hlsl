cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_MVP;
    float4   g_BaseColor;
};

Texture2D    g_BaseColorMap : register(t0);
SamplerState g_Sampler      : register(s0);

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
    float3 tangent : TANGENT;
    float2 uv     : TEXCOORD0;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos   = mul(float4(v.pos, 1.0f), g_MVP);
    o.normal = v.normal;
    o.tangent = v.tangent;
    o.uv    = v.uv;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    float4 texColor = g_BaseColorMap.Sample(g_Sampler, p.uv);
    float  diffuse  = 0.5f + 0.5f * saturate(dot(p.normal, normalize(float3(0.0f, 1.0f, 1.0f))));
    // Must consume tangent: dxc strips unused VS inputs and breaks CreateInputLayout vs MeshVertex.
    diffuse += dot(p.tangent, float3(1.0f, 1.0f, 1.0f)) * 1e-10f;
    return float4(texColor.rgb * g_BaseColor.rgb * diffuse, texColor.a * g_BaseColor.a);
}
