cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_ViewProj;
    row_major float4x4 g_World;
    row_major float4x4 g_LightViewProj;
    row_major float4x4 g_LightViewProjCascade[3];
    float4 g_CascadeSplits;
    row_major float4x4 g_SpotLightViewProj;
    float4 g_BaseColor;
    float4 g_LightDirection;
    float4 g_LightColor;
    float4 g_CameraPosition;
    float4 g_Material;
    float4 g_Emissive;
    float4 g_MapFlags;
    float4 g_PointLightPositions[4];
    float4 g_PointLightColors[4];
    float4 g_SpotLightPositions[4];
    float4 g_SpotLightDirections[4];
    float4 g_SpotLightColors[4];
    float4 g_SpotLightParams[4];
    float4 g_LightInfo;
    float4 g_PointShadowPosition;
    float4 g_ShadowInfo;
    float4 g_PostProcess;
    float4 g_PostProcess2;
    row_major float4x4 g_InstanceWorld[64];
    row_major float4x4 g_InstanceNormal[64];
    float4 g_DrawInfo;
    row_major float4x4 g_BoneMatrices[128];
    float4 g_SkinInfo;
    float4 g_IBLInfo;
    row_major float4x4 g_NormalMatrix;
    float4 g_CameraForward;
};

Texture2D    g_BaseColorMap : register(t0);
SamplerState g_Sampler      : register(s0);
Texture2DArray g_ShadowMap  : register(t1);
SamplerComparisonState g_ShadowSampler : register(s1);
Texture2D    g_NormalMap : register(t2);
SamplerState g_NormalSampler : register(s2);
Texture2D    g_MetallicRoughnessMap : register(t3);
SamplerState g_MetallicRoughnessSampler : register(s3);
Texture2D    g_OcclusionMap : register(t4);
SamplerState g_OcclusionSampler : register(s4);
Texture2D    g_EmissiveMap : register(t5);
SamplerState g_EmissiveSampler : register(s5);
Texture2D    g_SpotShadowMap : register(t6);
SamplerState g_SpotShadowSampler : register(s6);
TextureCube  g_PointShadowMap : register(t7);
SamplerState g_PointShadowSampler : register(s7);
TextureCube  g_IBLCubemap : register(t8);
SamplerState g_IBLSampler : register(s8);
StructuredBuffer<float4> g_EnvironmentSH2 : register(t9);

#include "PBR_BRDF.hlsli"
#include "EnvironmentRadiance.hlsli"

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
    float4 joints  : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};

struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 normalW  : NORMAL;
    float3 tangentW : TANGENT;
    float2 uv       : TEXCOORD0;
    float4 lightPos : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

VSOut VSMain(VSIn v, uint instanceId : SV_InstanceID)
{
    VSOut o;
    float4 localPosition = float4(v.pos, 1.0f);
    float3 localNormal = v.normal;
    float3 localTangent = v.tangent;
    if (g_SkinInfo.x > 0.5f) {
        row_major float4x4 skin =
            g_BoneMatrices[(uint)v.joints.x] * v.weights.x +
            g_BoneMatrices[(uint)v.joints.y] * v.weights.y +
            g_BoneMatrices[(uint)v.joints.z] * v.weights.z +
            g_BoneMatrices[(uint)v.joints.w] * v.weights.w;
        localPosition = mul(localPosition, skin);
        localNormal = mul(float4(localNormal, 0.0f), skin).xyz;
        localTangent = mul(float4(localTangent, 0.0f), skin).xyz;
    }
    row_major float4x4 world = g_World;
    row_major float4x4 normalMatrix = g_NormalMatrix;
    if (g_DrawInfo.x > 0.5f) {
        world = g_InstanceWorld[instanceId];
        normalMatrix = g_InstanceNormal[instanceId];
    }
    float4 worldPos = mul(localPosition, world);
    o.pos      = mul(worldPos, g_ViewProj);
    o.lightPos = mul(worldPos, g_LightViewProj);
    o.normalW  = normalize(mul(float4(localNormal, 0.0f), normalMatrix).xyz);
    o.tangentW = mul(float4(localTangent, 0.0f), normalMatrix).xyz;
    o.uv       = v.uv;
    o.worldPos = worldPos.xyz;
    return o;
}



void EvalSH2(float3 d, out float sh[9])
{
    sh[0] = 0.282095f;
    sh[1] = 0.488603f * d.y;
    sh[2] = 0.488603f * d.z;
    sh[3] = 0.488603f * d.x;
    sh[4] = 1.092548f * d.x * d.y;
    sh[5] = 1.092548f * d.y * d.z;
    sh[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    sh[7] = 1.092548f * d.x * d.z;
    sh[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

float3 EvaluateEnvironmentSH2(float3 direction)
{
    float basis[9];
    EvalSH2(direction, basis);
    float3 color = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i) {
        color += g_EnvironmentSH2[i].rgb * basis[i];
    }
    return max(color, 0.0f);
}



float SampleProjectedShadow(Texture2D shadowMap, SamplerState shadowSampler,
                            float4 lightClip, float nDotL)
{
    float3 proj = lightClip.xyz / max(lightClip.w, 1e-5f);
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f) {
        return 1.0f;
    }

    float bias = max(0.0020f, 0.0100f * (1.0f - nDotL));
    float shadowDepth = shadowMap.Sample(shadowSampler, uv).r;
    return ((proj.z - bias) <= shadowDepth) ? 1.0f : 0.35f;
}

float SampleDirectionalCascade(float3 worldPos, float nDotL, uint cascade)
{
    float4 lightClip = mul(float4(worldPos, 1.0f), g_LightViewProjCascade[cascade]);
    float3 proj = lightClip.xyz / max(lightClip.w, 1e-5f);
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f) {
        return 1.0f;
    }

    float bias = max(0.0020f, 0.0100f * (1.0f - nDotL));
    float compareDepth = proj.z - bias;

    const float2 texelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);
    float shadow = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offsetUv = uv + float2(x, y) * texelSize;
            shadow += g_ShadowMap.SampleCmpLevelZero(
                g_ShadowSampler, float3(offsetUv, (float)cascade), compareDepth);
        }
    }
    return lerp(0.35f, 1.0f, shadow / 9.0f);
}

float SampleDirectionalShadow(float3 worldPos, float nDotL)
{
    float3 toCamera = worldPos - g_CameraPosition.xyz;
    float viewDepth = abs(dot(toCamera, g_CameraForward.xyz));

    uint cascade = 0;
    if (viewDepth > g_CascadeSplits.y) {
        cascade = 2;
    } else if (viewDepth > g_CascadeSplits.x) {
        cascade = 1;
    }

    return SampleDirectionalCascade(worldPos, nDotL, cascade);
}

float SamplePointShadow(float3 worldPos, float nDotL)
{
    float3 lightToFragment = worldPos - g_PointShadowPosition.xyz;
    float distanceToLight = length(lightToFragment);
    float farPlane = max(g_PointShadowPosition.w, 0.1f);
    if (distanceToLight <= 1e-4f || distanceToLight >= farPlane) {
        return 1.0f;
    }

    float faceDepth = max(abs(lightToFragment.x),
        max(abs(lightToFragment.y), abs(lightToFragment.z)));
    float nearPlane = max(g_ShadowInfo.w, 0.001f);
    float currentDepth = farPlane / (farPlane - nearPlane) -
        (nearPlane * farPlane) / ((farPlane - nearPlane) * faceDepth);
    float sampledDepth = g_PointShadowMap.Sample(
        g_PointShadowSampler, lightToFragment).r;
    float bias = max(0.0030f, 0.0150f * (1.0f - nDotL));
    return ((currentDepth - bias) <= sampledDepth) ? 1.0f : 0.35f;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    float4 texColor = g_BaseColorMap.Sample(g_Sampler, p.uv);
    if (g_Emissive.w > 0.5f && texColor.a * g_BaseColor.a < g_Material.w) {
        discard;
    }
    float3 albedo = pow(max(texColor.rgb * g_BaseColor.rgb, 0.0f), 2.2f);
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

    float3 L = normalize(-g_LightDirection.xyz);
    float3 N = normalize(p.normalW);
    if (g_MapFlags.x > 0.5f) {
        float3 tangent = normalize(p.tangentW - N * dot(p.tangentW, N));
        float3 bitangent = normalize(cross(N, tangent));
        float3 normalTS = g_NormalMap.Sample(g_NormalSampler, p.uv).xyz * 2.0f - 1.0f;
        N = normalize(normalTS.x * tangent + normalTS.y * bitangent + normalTS.z * N);
    }
    float3 V = normalize(g_CameraPosition.xyz - p.worldPos);
    float NdotL = saturate(dot(N, L));

    float shadow = 1.0f;
    if (g_ShadowInfo.x > 0.5f) {
        shadow = SampleDirectionalShadow(p.worldPos, NdotL);
    }

    float3 direct = PbrDirectLighting(
        albedo, metallic, roughness, N, V, L,
        g_LightColor.rgb * max(g_LightDirection.w, 0.0f) * shadow);

    uint pointCount = min((uint)g_LightInfo.x, 4u);
    for (uint lightIndex = 0; lightIndex < pointCount; ++lightIndex) {
        float3 toLight = g_PointLightPositions[lightIndex].xyz - p.worldPos;
        float distanceToLight = length(toLight);
        float range = max(g_PointLightPositions[lightIndex].w, 0.01f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        if (attenuation <= 0.0f || distanceToLight <= 1e-4f) {
            continue;
        }

        float3 pointL = toLight / distanceToLight;
        float pointNdotL = saturate(dot(N, pointL));
        float pointShadow = 1.0f;
        if ((int)g_ShadowInfo.z == (int)lightIndex) {
            pointShadow = SamplePointShadow(p.worldPos, pointNdotL);
        }
        direct += PbrDirectLighting(
            albedo, metallic, roughness, N, V, pointL,
            g_PointLightColors[lightIndex].rgb *
            max(g_PointLightColors[lightIndex].w, 0.0f) * attenuation * pointShadow);
    }

    uint spotCount = min((uint)g_LightInfo.z, 4u);
    for (uint spotIndex = 0; spotIndex < spotCount; ++spotIndex) {
        float3 toLight = g_SpotLightPositions[spotIndex].xyz - p.worldPos;
        float distanceToLight = length(toLight);
        float range = max(g_SpotLightPositions[spotIndex].w, 0.01f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        if (attenuation <= 0.0f || distanceToLight <= 1e-4f) {
            continue;
        }

        float3 spotL = toLight / distanceToLight;
        float3 spotDirection = normalize(g_SpotLightDirections[spotIndex].xyz);
        float coneCos = dot(-spotL, spotDirection);
        float innerCone = g_SpotLightParams[spotIndex].x;
        float outerCone = g_SpotLightParams[spotIndex].y;
        float coneAttenuation = saturate((coneCos - outerCone) / max(innerCone - outerCone, 1e-4f));
        coneAttenuation *= coneAttenuation;
        if (coneAttenuation <= 0.0f) {
            continue;
        }

        float spotNdotL = saturate(dot(N, spotL));
        float spotShadow = 1.0f;
        if ((int)g_ShadowInfo.y == (int)spotIndex) {
            float4 spotLightPos = mul(float4(p.worldPos, 1.0f), g_SpotLightViewProj);
            spotShadow = SampleProjectedShadow(
                g_SpotShadowMap, g_SpotShadowSampler, spotLightPos, spotNdotL);
        }
        direct += PbrDirectLighting(
            albedo, metallic, roughness, N, V, spotL,
            g_SpotLightColors[spotIndex].rgb *
            max(g_SpotLightColors[spotIndex].w, 0.0f) *
            attenuation * coneAttenuation * spotShadow);
    }

    float3 reflectionDirection = reflect(-V, N);
    float3 ambient;
    if (g_IBLInfo.x > 0.5f) {
        float3 irradiance = EvaluateEnvironmentSH2(N);
        float mipLevel = roughness * 6.0f;
        float3 prefilteredColor = g_IBLCubemap.SampleLevel(
            g_IBLSampler, reflectionDirection, mipLevel).rgb;
        ambient = PbrEnvironmentLighting(
            albedo, metallic, roughness, ao, N, V, irradiance, prefilteredColor) *
            max(g_LightInfo.y, 0.0f) * max(g_IBLInfo.y, 0.0f);
    } else {
        float3 environmentDiffuse = EnvironmentRadiance(N) / ENV_PI;
        float3 environmentSpecular = EnvironmentRadiance(
            normalize(lerp(reflectionDirection, N, roughness * roughness)));
        ambient = PbrEnvironmentLighting(
            albedo, metallic, roughness, ao, N, V, environmentDiffuse, environmentSpecular) *
            max(g_LightInfo.y, 0.0f);
    }
    float3 emissive = g_Emissive.rgb;
    if (g_MapFlags.w > 0.5f) {
        emissive *= pow(max(g_EmissiveMap.Sample(g_EmissiveSampler, p.uv).rgb, 0.0f), 2.2f);
    }
    float3 color = ambient + direct + emissive;

    if (g_PostProcess2.w >= 0.0f)
    {
        color *= max(g_PostProcess.x, 0.0f);
        if (g_PostProcess.z > 0.5f) {
            color = AcesToneMap(color);
        }
        float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = lerp(float3(luminance, luminance, luminance), color, max(g_PostProcess2.x, 0.0f));
        color = (color - 0.5f) * max(g_PostProcess2.y, 0.0f) + 0.5f;

        float2 centeredUv = p.uv * 2.0f - 1.0f;
        float vignette = saturate(1.0f - dot(centeredUv, centeredUv) * g_PostProcess.w);
        color *= vignette;

        float edgeAmount = saturate((length(ddx(color)) + length(ddy(color))) * 12.0f);
        float3 aaColor = lerp(color, float3(luminance, luminance, luminance), edgeAmount * 0.15f);
        color = lerp(color, aaColor, saturate(g_PostProcess2.z));

        color = saturate(color);
        color = pow(color, 1.0f / max(g_PostProcess.y, 0.1f));
    }

    color += dot(p.tangentW, float3(1.0f, 1.0f, 1.0f)) * 1e-10f;
    return float4(color, texColor.a * g_BaseColor.a);
}
