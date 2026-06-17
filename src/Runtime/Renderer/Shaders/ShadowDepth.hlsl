cbuffer ShadowPerDraw : register(b0)
{
    row_major float4x4 g_LightMVP;
    row_major float4x4 g_BoneMatrices[128];
    float4 g_SkinInfo;
};

struct VSIn
{
    float3 pos : POSITION;
    float4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 localPosition = float4(v.pos, 1.0f);
    if (g_SkinInfo.x > 0.5f) {
        row_major float4x4 skin =
            g_BoneMatrices[(uint)v.joints.x] * v.weights.x +
            g_BoneMatrices[(uint)v.joints.y] * v.weights.y +
            g_BoneMatrices[(uint)v.joints.z] * v.weights.z +
            g_BoneMatrices[(uint)v.joints.w] * v.weights.w;
        localPosition = mul(localPosition, skin);
    }
    o.pos = mul(localPosition, g_LightMVP);
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return 1.0f.xxxx;
}
