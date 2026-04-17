cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_MVP;
    row_major float4x4 g_World;
    row_major float4x4 g_LightViewProj;
    float4 g_BaseColor;
    float4 g_LightDirection;
};

Texture2D    g_BaseColorMap : register(t0);
SamplerState g_Sampler      : register(s0);
Texture2D    g_ShadowMap    : register(t1);
SamplerState g_ShadowSampler : register(s1);

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
};

struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 normalW  : NORMAL;
    float3 tangentW : TANGENT;
    float2 uv       : TEXCOORD0;
    float4 lightPos : TEXCOORD1;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 worldPos = mul(float4(v.pos, 1.0f), g_World);
    o.pos      = mul(worldPos, g_MVP);
    o.lightPos = mul(worldPos, g_LightViewProj);
    o.normalW  = normalize(mul(float4(v.normal, 0.0f), g_World).xyz);
    o.tangentW = mul(float4(v.tangent, 0.0f), g_World).xyz;
    o.uv       = v.uv;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    float4 texColor = g_BaseColorMap.Sample(g_Sampler, p.uv);
    float3 L = normalize(-g_LightDirection.xyz);
    float3 N = normalize(p.normalW);
    float NdotL = saturate(dot(N, L));
    float diffuse = 0.2f + 0.8f * NdotL;
    diffuse += dot(p.tangentW, float3(1.0f, 1.0f, 1.0f)) * 1e-10f;

    float shadow = 1.0f;
    float3 proj = p.lightPos.xyz / max(p.lightPos.w, 1e-5f);
    float2 suv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (suv.x >= 0.0f && suv.x <= 1.0f &&
        suv.y >= 0.0f && suv.y <= 1.0f &&
        proj.z >= 0.0f && proj.z <= 1.0f) {
        const float bias = max(0.0020f, 0.0100f * (1.0f - NdotL));
        float shadowDepth = g_ShadowMap.Sample(g_ShadowSampler, suv).r;
        shadow = ((proj.z - bias) <= shadowDepth) ? 1.0f : 0.35f;
    }

    float3 lit = texColor.rgb * g_BaseColor.rgb * diffuse * shadow;
    return float4(lit, texColor.a * g_BaseColor.a);
}
