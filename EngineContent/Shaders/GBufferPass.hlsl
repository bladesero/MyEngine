cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_ViewProj;
    row_major float4x4 g_World;
    row_major float4x4 g_PreviousViewProj;
    row_major float4x4 g_PreviousWorld;
    float4 g_BaseColor;
    float4 g_Material;
    float4 g_Emissive;
    float4 g_MapFlags;
    row_major float4x4 g_BoneMatrices[128];
    row_major float4x4 g_PreviousBoneMatrices[128];
    float4 g_SkinInfo;
    row_major float4x4 g_NormalMatrix;
};

Texture2D    g_BaseColorMap : register(t0);
SamplerState g_Sampler      : register(s0);
Texture2D    g_NormalMap : register(t2);
SamplerState g_NormalSampler : register(s2);
Texture2D    g_MetallicRoughnessMap : register(t3);
SamplerState g_MetallicRoughnessSampler : register(s3);
Texture2D    g_OcclusionMap : register(t4);
SamplerState g_OcclusionSampler : register(s4);
Texture2D    g_EmissiveMap : register(t5);
SamplerState g_EmissiveSampler : register(s5);

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
    float4 joints  : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
    float4 color   : COLOR0;
};

struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 normalW  : NORMAL;
    float3 tangentW : TANGENT;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    float4 currentClip : TEXCOORD1;
    float4 previousClip : TEXCOORD2;
};

struct PSOut
{
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float4 emissive : SV_Target3;
    float2 velocity : SV_Target4;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 localPosition = float4(v.pos, 1.0f);
    float4 previousLocalPosition = localPosition;
    float3 localNormal = v.normal;
    float3 localTangent = v.tangent;
    if (g_SkinInfo.x > 0.5f) {
        row_major float4x4 skin =
            g_BoneMatrices[(uint)v.joints.x] * v.weights.x +
            g_BoneMatrices[(uint)v.joints.y] * v.weights.y +
            g_BoneMatrices[(uint)v.joints.z] * v.weights.z +
            g_BoneMatrices[(uint)v.joints.w] * v.weights.w;
        localPosition = mul(localPosition, skin);
        row_major float4x4 previousSkin =
            g_PreviousBoneMatrices[(uint)v.joints.x] * v.weights.x +
            g_PreviousBoneMatrices[(uint)v.joints.y] * v.weights.y +
            g_PreviousBoneMatrices[(uint)v.joints.z] * v.weights.z +
            g_PreviousBoneMatrices[(uint)v.joints.w] * v.weights.w;
        previousLocalPosition = mul(previousLocalPosition, previousSkin);
        localNormal = mul(float4(localNormal, 0.0f), skin).xyz;
        localTangent = mul(float4(localTangent, 0.0f), skin).xyz;
    }

    float4 worldPos = mul(localPosition, g_World);
    o.pos = mul(worldPos, g_ViewProj);
    o.currentClip = o.pos;
    o.previousClip = mul(mul(previousLocalPosition, g_PreviousWorld), g_PreviousViewProj);
    o.normalW = normalize(mul(float4(localNormal, 0.0f), g_NormalMatrix).xyz);
    o.tangentW = mul(float4(localTangent, 0.0f), g_NormalMatrix).xyz;
    o.uv = v.uv;
    o.color = v.color;
    return o;
}

PSOut PSMain(VSOut p)
{
    PSOut o;

    float4 texColor = g_BaseColorMap.Sample(g_Sampler, p.uv);
    float alpha = texColor.a * g_BaseColor.a * p.color.a;
    if (g_Emissive.w > 0.5f && alpha < g_Material.w) {
        discard;
    }

    float3 albedo = pow(max(texColor.rgb * g_BaseColor.rgb * p.color.rgb, 0.0f), 2.2f);
    float metallic = saturate(g_Material.x);
    float roughness = clamp(g_Material.y, 0.04f, 1.0f);
    float ao = max(g_Material.z, 0.0f);
    if (g_MapFlags.y > 0.5f) {
        float4 mr = g_MetallicRoughnessMap.Sample(g_MetallicRoughnessSampler, p.uv);
        metallic *= mr.b;
        roughness = clamp(roughness * mr.g, 0.04f, 1.0f);
    }
    if (g_MapFlags.z > 0.5f) {
        ao *= g_OcclusionMap.Sample(g_OcclusionSampler, p.uv).r;
    }

    float3 N = normalize(p.normalW);
    if (g_MapFlags.x > 0.5f) {
        float3 tangent = normalize(p.tangentW - N * dot(p.tangentW, N));
        float3 bitangent = normalize(cross(N, tangent));
        float3 normalTS = g_NormalMap.Sample(g_NormalSampler, p.uv).xyz * 2.0f - 1.0f;
        N = normalize(normalTS.x * tangent + normalTS.y * bitangent + normalTS.z * N);
    }

    float3 emissive = g_Emissive.rgb;
    if (g_MapFlags.w > 0.5f) {
        emissive *= pow(max(g_EmissiveMap.Sample(g_EmissiveSampler, p.uv).rgb, 0.0f), 2.2f);
    }

    o.albedo = float4(albedo, alpha);
    o.normal = float4(N * 0.5f + 0.5f, 1.0f);
    o.material = float4(metallic, roughness, ao, 0.0f);
    o.emissive = float4(emissive, g_Emissive.w);
    float2 currentNdc = p.currentClip.xy / max(abs(p.currentClip.w), 1e-5f);
    float2 previousNdc = p.previousClip.xy / max(abs(p.previousClip.w), 1e-5f);
    o.velocity = (currentNdc - previousNdc) * float2(0.5f, -0.5f);
    return o;
}
