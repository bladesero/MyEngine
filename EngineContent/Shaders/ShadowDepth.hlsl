cbuffer ShadowPerDraw : register(b0)
{
    row_major float4x4 g_LightMVP;
#if defined(MYENGINE_SHADOW_SKINNED)
    row_major float4x4 g_BoneMatrices[128];
    float4 g_SkinInfo;
#endif
    // x = alpha threshold, y = alpha-test enabled, z = material base alpha.
    float4 g_AlphaTest;
};

Texture2D<float4> g_BaseColorMap : register(t0);
SamplerState g_BaseColorSampler : register(s0);

struct VSIn
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
#if defined(MYENGINE_SHADOW_SKINNED)
    float4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
#endif
    float4 color : COLOR0;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float alpha : COLOR0;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 localPosition = float4(v.pos, 1.0f);
#if defined(MYENGINE_SHADOW_SKINNED)
    if (g_SkinInfo.x > 0.5f) {
        row_major float4x4 skin =
            g_BoneMatrices[(uint)v.joints.x] * v.weights.x +
            g_BoneMatrices[(uint)v.joints.y] * v.weights.y +
            g_BoneMatrices[(uint)v.joints.z] * v.weights.z +
            g_BoneMatrices[(uint)v.joints.w] * v.weights.w;
        localPosition = mul(localPosition, skin);
    }
#endif
    o.pos = mul(localPosition, g_LightMVP);
    o.uv = v.uv;
    o.alpha = v.color.a;
    return o;
}

void PSMain(VSOut input)
{
    if (g_AlphaTest.y > 0.5f) {
        const float alpha =
            g_BaseColorMap.Sample(g_BaseColorSampler, input.uv).a * g_AlphaTest.z * input.alpha;
        clip(alpha - g_AlphaTest.x);
    }
}
