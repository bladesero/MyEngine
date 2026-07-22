Texture2D g_GBufferAlbedo : register(t0);
Texture2D g_GBufferNormal : register(t1);
Texture2D g_GBufferMaterial : register(t2);
Texture2D g_GBufferEmissive : register(t3);
Texture2D g_SceneDepth : register(t4);
Texture2DArray g_ShadowMap : register(t5);
Texture2D g_SpotShadowMap : register(t6);
TextureCube g_PointShadowMap : register(t7);
TextureCube g_IBLCubemap : register(t8);
StructuredBuffer<float4> g_EnvironmentSH2 : register(t9);
#include "ProbeLighting.hlsli"
Texture2DArray<float4> g_LocalReflectionProbes : register(t10);
StructuredBuffer<ReflectionProbeGpuData> g_LocalReflectionProbeData : register(t11);
StructuredBuffer<SHProbeVolumeGpuData> g_LocalSHProbeVolumes : register(t12);
StructuredBuffer<float4> g_LocalSHCoefficients : register(t13);

SamplerState g_LinearSampler : register(s0);
SamplerState g_PointSampler : register(s1);
SamplerComparisonState g_ShadowSampler : register(s2);

cbuffer DeferredLightingParams : register(b0)
{
    row_major float4x4 g_ViewProj;
    row_major float4x4 g_InvViewProj;
    row_major float4x4 g_LightViewProj;
    row_major float4x4 g_LightViewProjCascade[3];
    float4 g_CascadeSplits;
    row_major float4x4 g_SpotLightViewProj;
    float4 g_LightDirection;
    float4 g_LightColor;
    float4 g_CameraPosition;
    float4 g_PointLightPositions[4];
    float4 g_PointLightColors[4];
    float4 g_SpotLightPositions[4];
    float4 g_SpotLightDirections[4];
    float4 g_SpotLightColors[4];
    float4 g_SpotLightParams[4];
    float4 g_LightInfo;
    float4 g_PointShadowPosition;
    float4 g_ShadowInfo;
    float4 g_ShadowIntensity;
    float4 g_IBLInfo;
    float4 g_ScreenSize;
    uint g_LocalReflectionProbeCount;
    uint g_LocalSHProbeVolumeCount;
    float g_LocalReflectionMipCount;
    float g_ProbeLightingPadding;
    float4 g_EnvironmentLighting;
    float4 g_SkyTint;
    float4 g_HorizonTint;
    float4 g_GroundTint;
};

#include "PBR_BRDF.hlsli"
#include "EnvironmentRadiance.hlsli"

static const float DIRECT_SHADOW_MIN_VISIBILITY = 0.0f;

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    o.uv = float2((vertexId << 1) & 2, vertexId & 2);
    o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float3 WorldPosFromDepth(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(clip, g_InvViewProj);
    return world.xyz / max(world.w, 1e-6f);
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
            shadow += g_ShadowMap.SampleCmpLevelZero(
                g_ShadowSampler, float3(uv + float2(x, y) * texelSize, (float)cascade), compareDepth);
        }
    }
    // CSM visibility scales the complete direct-light BRDF, including specular. A non-zero floor leaks bright
    // LightComponent highlights through fully occluded pixels; ambient/environment lighting is added separately.
    return shadow / 9.0f;
}

float SampleDirectionalShadow(float3 worldPos, float nDotL)
{
    float viewDepth = max(length(worldPos - g_CameraPosition.xyz), 0.0f);
    uint cascade = 0;
    if (viewDepth > g_CascadeSplits.y) {
        cascade = 2;
    } else if (viewDepth > g_CascadeSplits.x) {
        cascade = 1;
    }
    return SampleDirectionalCascade(worldPos, nDotL, cascade);
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
    return ((proj.z - bias) <= shadowDepth) ? 1.0f : DIRECT_SHADOW_MIN_VISIBILITY;
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
    float sampledDepth = g_PointShadowMap.Sample(g_LinearSampler, lightToFragment).r;
    float bias = max(0.0030f, 0.0150f * (1.0f - nDotL));
    return ((currentDepth - bias) <= sampledDepth) ? 1.0f : DIRECT_SHADOW_MIN_VISIBILITY;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float depth = g_SceneDepth.SampleLevel(g_PointSampler, input.uv, 0).r;
    float3 worldPos = WorldPosFromDepth(input.uv, depth);
    float3 viewDir = normalize(worldPos - g_CameraPosition.xyz);
    float3 sunDirection = normalize(-g_LightDirection.xyz);
    if (depth >= 0.9999f) {
        float3 sky = EnvironmentRadiance(
            viewDir, sunDirection, g_SkyTint.rgb, g_HorizonTint.rgb, g_GroundTint.rgb);
        return float4(sky * max(g_EnvironmentLighting.w, 0.0f), 1.0f);
    }

    float4 albedoAlpha = g_GBufferAlbedo.SampleLevel(g_LinearSampler, input.uv, 0);
    float3 albedo = albedoAlpha.rgb;
    float3 N = normalize(g_GBufferNormal.SampleLevel(g_LinearSampler, input.uv, 0).xyz * 2.0f - 1.0f);
    float4 material = g_GBufferMaterial.SampleLevel(g_PointSampler, input.uv, 0);
    float3 emissive = g_GBufferEmissive.SampleLevel(g_LinearSampler, input.uv, 0).rgb;
    float metallic = saturate(material.x);
    float roughness = clamp(material.y, 0.04f, 1.0f);
    float ao = max(material.z, 0.0f);
    float3 V = normalize(g_CameraPosition.xyz - worldPos);

    float3 L = sunDirection;
    float nDotL = saturate(dot(N, L));
    float shadow = 1.0f;
    if (g_ShadowInfo.x > 0.5f) {
        shadow = lerp(1.0f, SampleDirectionalShadow(worldPos, nDotL),
            saturate(g_ShadowIntensity.x));
    }

    float3 direct = PbrDirectLighting(
        albedo, metallic, roughness, N, V, L,
        g_LightColor.rgb * max(g_LightDirection.w, 0.0f) * shadow);

    uint pointCount = min((uint)g_LightInfo.x, 4u);
    for (uint lightIndex = 0; lightIndex < pointCount; ++lightIndex) {
        float3 toLight = g_PointLightPositions[lightIndex].xyz - worldPos;
        float distanceToLight = length(toLight);
        float range = max(g_PointLightPositions[lightIndex].w, 0.01f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        if (attenuation <= 0.0f || distanceToLight <= 1e-4f) continue;

        float3 pointL = toLight / distanceToLight;
        float pointNdotL = saturate(dot(N, pointL));
        float pointShadow = 1.0f;
        if ((int)g_ShadowInfo.z == (int)lightIndex) {
            pointShadow = lerp(1.0f, SamplePointShadow(worldPos, pointNdotL),
                saturate(g_ShadowIntensity.z));
        }
        direct += PbrDirectLighting(
            albedo, metallic, roughness, N, V, pointL,
            g_PointLightColors[lightIndex].rgb *
            max(g_PointLightColors[lightIndex].w, 0.0f) * attenuation * pointShadow);
    }

    uint spotCount = min((uint)g_LightInfo.z, 4u);
    for (uint spotIndex = 0; spotIndex < spotCount; ++spotIndex) {
        float3 toLight = g_SpotLightPositions[spotIndex].xyz - worldPos;
        float distanceToLight = length(toLight);
        float range = max(g_SpotLightPositions[spotIndex].w, 0.01f);
        float attenuation = saturate(1.0f - distanceToLight / range);
        attenuation *= attenuation;
        if (attenuation <= 0.0f || distanceToLight <= 1e-4f) continue;

        float3 spotL = toLight / distanceToLight;
        float3 spotDirection = normalize(g_SpotLightDirections[spotIndex].xyz);
        float coneCos = dot(-spotL, spotDirection);
        float innerCone = g_SpotLightParams[spotIndex].x;
        float outerCone = g_SpotLightParams[spotIndex].y;
        float coneAttenuation = saturate((coneCos - outerCone) / max(innerCone - outerCone, 1e-4f));
        coneAttenuation *= coneAttenuation;
        if (coneAttenuation <= 0.0f) continue;

        float spotNdotL = saturate(dot(N, spotL));
        float spotShadow = 1.0f;
        if ((int)g_ShadowInfo.y == (int)spotIndex) {
            float4 spotLightPos = mul(float4(worldPos, 1.0f), g_SpotLightViewProj);
            spotShadow = lerp(1.0f, SampleProjectedShadow(
                g_SpotShadowMap, g_LinearSampler, spotLightPos, spotNdotL),
                saturate(g_ShadowIntensity.y));
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
        float3 globalIrradiance = EvaluateEnvironmentSH2(N);
        float3 irradiance = EvaluateLocalSHVolumes(g_LocalSHProbeVolumes, g_LocalSHCoefficients,
            g_LocalSHProbeVolumeCount, worldPos, N, globalIrradiance);
        float3 globalPrefilteredColor = g_IBLCubemap.SampleLevel(
            g_LinearSampler, reflectionDirection, roughness * 6.0f).rgb;
        float3 prefilteredColor = SampleLocalReflectionsAuto(g_LocalReflectionProbes, g_LinearSampler,
            g_LocalReflectionProbeData, g_LocalReflectionProbeCount, worldPos, reflectionDirection,
            roughness, g_LocalReflectionMipCount, globalPrefilteredColor);
        ambient = PbrEnvironmentLighting(
            albedo, metallic, roughness, ao, N, V, irradiance, prefilteredColor) *
            max(g_LightInfo.y, 0.0f) * max(g_IBLInfo.y, 0.0f) * max(g_EnvironmentLighting.rgb, 0.0f);
    } else {
        float3 environmentDiffuse = EnvironmentRadiance(
            N, sunDirection, g_SkyTint.rgb, g_HorizonTint.rgb, g_GroundTint.rgb) / ENV_PI;
        float3 environmentSpecular = EnvironmentRadiance(
            normalize(lerp(reflectionDirection, N, roughness * roughness)), sunDirection,
            g_SkyTint.rgb, g_HorizonTint.rgb, g_GroundTint.rgb);
        ambient = PbrEnvironmentLighting(
            albedo, metallic, roughness, ao, N, V, environmentDiffuse, environmentSpecular) *
            max(g_LightInfo.y, 0.0f) * max(g_EnvironmentLighting.rgb, 0.0f);
    }

    return float4(ambient + direct + emissive, albedoAlpha.a);
}
