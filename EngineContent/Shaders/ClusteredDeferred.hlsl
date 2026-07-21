static const uint CLUSTER_Z_SLICES = 24;
static const uint MAX_LIGHTS_PER_CLUSTER = 128;

struct GpuSceneLight
{
    float4 positionRange;
    float4 directionType;
    float4 colorIntensity;
    float4 spotAnglesShadow;
};

cbuffer ClusterConstants : register(b0)
{
    row_major float4x4 g_View;
    row_major float4x4 g_InverseProjection;
    row_major float4x4 g_InverseViewProjection;
    uint2 g_RenderSize;
    uint2 g_TileCount;
    uint g_ClusterCount;
    uint g_LightCount;
    float g_NearPlane;
    float g_FarPlane;
    float3 g_CameraPosition;
    float g_ClusterPadding;
    float4 g_DirectionalLight;
    float4 g_DirectionalColorAmbient;
    float4 g_EnvironmentLighting;
    row_major float4x4 g_ShadowViewProjection[3];
    float4 g_CascadeSplits;
    float4 g_ShadowInfo;
    uint g_LocalReflectionProbeCount;
    uint g_LocalSHProbeVolumeCount;
    float g_LocalReflectionMipCount;
    float g_ProbeLightingPadding;
};

#include "PBR_BRDF.hlsli"
#include "ModernReflection.hlsli"

StructuredBuffer<GpuSceneLight> g_Lights : register(t0);
StructuredBuffer<uint> g_ClusterCounts : register(t1);
StructuredBuffer<uint> g_ClusterOffsets : register(t2);
StructuredBuffer<uint> g_ClusterLightIndices : register(t3);
RWStructuredBuffer<uint> g_ClusterCountsOut : register(u0);
RWStructuredBuffer<uint> g_ClusterOffsetsOut : register(u1);
RWStructuredBuffer<uint> g_ClusterLightIndicesOut : register(u2);
RWStructuredBuffer<uint> g_ClusterOverflow : register(u3);

Texture2D<float4> g_GBufferAlbedo : register(t4);
Texture2D<float4> g_GBufferNormal : register(t5);
Texture2D<float4> g_GBufferMaterial : register(t6);
Texture2D<float4> g_GBufferEmissive : register(t7);
Texture2D<float> g_SceneDepth : register(t8);
TextureCube<float4> g_IBLCubemap : register(t9);
Texture2DArray<float> g_ShadowMap : register(t10);
StructuredBuffer<float4> g_EnvironmentSH2 : register(t11);
#include "ProbeLighting.hlsli"
Texture2DArray<float4> g_LocalReflectionProbes : register(t12);
StructuredBuffer<ReflectionProbeGpuData> g_LocalReflectionProbeData : register(t13);
StructuredBuffer<SHProbeVolumeGpuData> g_LocalSHProbeVolumes : register(t14);
StructuredBuffer<float4> g_LocalSHCoefficients : register(t15);
SamplerState g_LinearSampler : register(s0);
SamplerComparisonState g_ShadowSampler : register(s1);
RWTexture2D<float4> g_HdrOutput : register(u4);

float SliceNear(uint slice)
{
    return g_NearPlane * pow(g_FarPlane / g_NearPlane, (float)slice / (float)CLUSTER_Z_SLICES);
}

float3 ViewRay(float2 ndc)
{
    float4 clipPoint = mul(float4(ndc, 1.0f, 1.0f), g_InverseProjection);
    clipPoint.xyz /= max(abs(clipPoint.w), 1e-6f);
    return normalize(clipPoint.xyz);
}

void ClusterBounds(uint clusterIndex, out float3 boundsMin, out float3 boundsMax)
{
    uint slice = clusterIndex / (g_TileCount.x * g_TileCount.y);
    uint tileIndex = clusterIndex - slice * g_TileCount.x * g_TileCount.y;
    uint2 tile = uint2(tileIndex % g_TileCount.x, tileIndex / g_TileCount.x);
    float2 ndcMin = float2((float)(tile.x * 32) / g_RenderSize.x * 2.0f - 1.0f,
                           1.0f - (float)min((tile.y + 1) * 32, g_RenderSize.y) / g_RenderSize.y * 2.0f);
    float2 ndcMax = float2((float)min((tile.x + 1) * 32, g_RenderSize.x) / g_RenderSize.x * 2.0f - 1.0f,
                           1.0f - (float)(tile.y * 32) / g_RenderSize.y * 2.0f);
    float nearZ = SliceNear(slice);
    float farZ = SliceNear(slice + 1);
    float3 rays[4] = {ViewRay(float2(ndcMin.x, ndcMin.y)), ViewRay(float2(ndcMax.x, ndcMin.y)),
                      ViewRay(float2(ndcMin.x, ndcMax.y)), ViewRay(float2(ndcMax.x, ndcMax.y))};
    boundsMin = float3(1e30f, 1e30f, 1e30f);
    boundsMax = float3(-1e30f, -1e30f, -1e30f);
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float3 nearPoint = rays[i] * (nearZ / max(rays[i].z, 1e-5f));
        float3 farPoint = rays[i] * (farZ / max(rays[i].z, 1e-5f));
        boundsMin = min(boundsMin, min(nearPoint, farPoint));
        boundsMax = max(boundsMax, max(nearPoint, farPoint));
    }
}

bool LightIntersectsCluster(GpuSceneLight light, float3 boundsMin, float3 boundsMax)
{
    float3 viewPosition = mul(float4(light.positionRange.xyz, 1.0f), g_View).xyz;
    float3 closest = clamp(viewPosition, boundsMin, boundsMax);
    float3 delta = viewPosition - closest;
    return dot(delta, delta) <= light.positionRange.w * light.positionRange.w;
}

[numthreads(64, 1, 1)]
void CSClusterCount(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint cluster = dispatchThreadId.x;
    if (cluster >= g_ClusterCount)
        return;
    float3 boundsMin, boundsMax;
    ClusterBounds(cluster, boundsMin, boundsMax);
    uint count = 0;
    [loop]
    for (uint light = 0; light < g_LightCount; ++light)
        count += LightIntersectsCluster(g_Lights[light], boundsMin, boundsMax) ? 1 : 0;
    g_ClusterCountsOut[cluster] = min(count, MAX_LIGHTS_PER_CLUSTER);
    if (count > MAX_LIGHTS_PER_CLUSTER)
        InterlockedAdd(g_ClusterOverflow[0], count - MAX_LIGHTS_PER_CLUSTER);
}

[numthreads(1, 1, 1)]
void CSClusterPrefix(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint offset = 0;
    [loop]
    for (uint cluster = 0; cluster < g_ClusterCount; ++cluster)
    {
        g_ClusterOffsetsOut[cluster] = offset;
        offset += g_ClusterCounts[cluster];
    }
}

[numthreads(64, 1, 1)]
void CSClusterScatter(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint cluster = dispatchThreadId.x;
    if (cluster >= g_ClusterCount)
        return;
    float3 boundsMin, boundsMax;
    ClusterBounds(cluster, boundsMin, boundsMax);
    uint outputIndex = g_ClusterOffsets[cluster];
    uint written = 0;
    [loop]
    for (uint light = 0; light < g_LightCount && written < g_ClusterCounts[cluster]; ++light)
    {
        if (LightIntersectsCluster(g_Lights[light], boundsMin, boundsMax))
            g_ClusterLightIndicesOut[outputIndex + written++] = light;
    }
}

float3 ReconstructWorldPosition(uint2 pixel, float depth)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(g_RenderSize);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 world = mul(float4(ndc, depth, 1.0f), g_InverseViewProjection);
    return world.xyz / max(abs(world.w), 1e-6f);
}

void EvalSH2(float3 direction, out float basis[9])
{
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * direction.y;
    basis[2] = 0.488603f * direction.z;
    basis[3] = 0.488603f * direction.x;
    basis[4] = 1.092548f * direction.x * direction.y;
    basis[5] = 1.092548f * direction.y * direction.z;
    basis[6] = 0.315392f * (3.0f * direction.z * direction.z - 1.0f);
    basis[7] = 1.092548f * direction.x * direction.z;
    basis[8] = 0.546274f * (direction.x * direction.x - direction.y * direction.y);
}

float3 EvaluateEnvironmentSH2(float3 direction)
{
    float basis[9];
    EvalSH2(direction, basis);
    float3 color = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
        color += g_EnvironmentSH2[i].rgb * basis[i];
    return max(color, 0.0f);
}

float SampleDirectionalShadow(uint2 pixel, float3 worldPosition, float viewDepth, float nDotL)
{
    if (g_ShadowInfo.x <= 0.5f || g_ShadowInfo.z < 0.5f)
        return 1.0f;
    if (g_ShadowInfo.w > 0.5f)
        return g_ShadowMap.Load(int4(pixel, 0, 0));
    uint cascade = 0;
    if (g_ShadowInfo.z > 2.5f && viewDepth > g_CascadeSplits.y)
        cascade = 2;
    else if (g_ShadowInfo.z > 1.5f && viewDepth > g_CascadeSplits.x)
        cascade = 1;
    float4 lightClip = mul(float4(worldPosition, 1.0f), g_ShadowViewProjection[cascade]);
    float3 projected = lightClip.xyz / max(abs(lightClip.w), 1e-5f);
    float2 uv = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z < 0.0f || projected.z > 1.0f)
        return 1.0f;
    uint shadowWidth, shadowHeight, shadowLayers;
    g_ShadowMap.GetDimensions(shadowWidth, shadowHeight, shadowLayers);
    float2 texelSize = 1.0f / max(float2(shadowWidth, shadowHeight), 1.0f.xx);
    float compareDepth = projected.z - max(0.0008f, 0.004f * (1.0f - nDotL));
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
        visibility += g_ShadowMap.SampleCmpLevelZero(
            g_ShadowSampler, float3(uv + float2(x, y) * texelSize, (float)cascade), compareDepth);
    visibility /= 9.0f;
    return lerp(1.0f, lerp(0.08f, 1.0f, visibility), saturate(g_ShadowInfo.y));
}

[numthreads(8, 8, 1)]
void CSDeferredLighting(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RenderSize))
        return;
    float depth = g_SceneDepth.Load(int3(pixel, 0));
    float4 albedoSample = g_GBufferAlbedo.Load(int3(pixel, 0));
    float3 normal = normalize(g_GBufferNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
    float4 material = g_GBufferMaterial.Load(int3(pixel, 0));
    float3 emissive = g_GBufferEmissive.Load(int3(pixel, 0)).rgb;
    float3 sunDirection = normalize(-g_DirectionalLight.xyz);
    if (depth >= 1.0f)
    {
        float3 farWorldPosition = ReconstructWorldPosition(pixel, depth);
        float3 viewDirection = normalize(farWorldPosition - g_CameraPosition);
        g_HdrOutput[pixel] = float4(
            g_IBLCubemap.SampleLevel(g_LinearSampler, viewDirection, 0.0f).rgb *
                max(g_EnvironmentLighting.w, 0.0f),
            1.0f);
        return;
    }
    float3 worldPosition = ReconstructWorldPosition(pixel, depth);
    float viewDepth = mul(float4(worldPosition, 1.0f), g_View).z;
    float sliceValue = log(max(viewDepth, g_NearPlane) / g_NearPlane) /
                       log(g_FarPlane / g_NearPlane) * CLUSTER_Z_SLICES;
    uint slice = min((uint)max(sliceValue, 0.0f), CLUSTER_Z_SLICES - 1);
    uint2 tile = min(pixel / 32, g_TileCount - 1);
    uint cluster = (slice * g_TileCount.y + tile.y) * g_TileCount.x + tile.x;
    float3 viewDirection = normalize(g_CameraPosition - worldPosition);
    float metallic = saturate(material.x);
    float roughness = clamp(material.y, 0.04f, 1.0f);
    float ao = max(material.z, 0.0f);
    float nDotL = saturate(dot(normal, sunDirection));
    float directionalShadow = SampleDirectionalShadow(pixel, worldPosition, viewDepth, nDotL);
    float3 color = PbrDirectLighting(albedoSample.rgb, metallic, roughness, normal, viewDirection, sunDirection,
                                     g_DirectionalColorAmbient.rgb * max(g_DirectionalLight.w, 0.0f) *
                                         directionalShadow);
    float3 reflectionDirection = ModernWorldReflectionDirection(worldPosition, g_CameraPosition, normal);
    float3 globalEnvironmentDiffuse = EvaluateEnvironmentSH2(normal);
    float3 environmentDiffuse = EvaluateLocalSHVolumes(g_LocalSHProbeVolumes, g_LocalSHCoefficients,
        g_LocalSHProbeVolumeCount, worldPosition, normal, globalEnvironmentDiffuse);
    float3 globalEnvironmentSpecular =
        g_IBLCubemap.SampleLevel(g_LinearSampler, reflectionDirection, roughness * 6.0f).rgb;
    float3 environmentSpecular = SampleLocalReflectionsAuto(g_LocalReflectionProbes, g_LinearSampler,
        g_LocalReflectionProbeData, g_LocalReflectionProbeCount, worldPosition, reflectionDirection,
        roughness, g_LocalReflectionMipCount, globalEnvironmentSpecular);
    color += PbrEnvironmentLighting(albedoSample.rgb, metallic, roughness, ao, normal, viewDirection,
                                    environmentDiffuse, environmentSpecular) *
             max(g_DirectionalColorAmbient.w, 0.0f) * max(g_EnvironmentLighting.rgb, 0.0f);
    color += emissive;
    uint count = g_ClusterCounts[cluster];
    uint offset = g_ClusterOffsets[cluster];
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        GpuSceneLight light = g_Lights[g_ClusterLightIndices[offset + i]];
        float3 toLight = light.positionRange.xyz - worldPosition;
        float distance = length(toLight);
        float3 lightDirection = toLight / max(distance, 1e-5f);
        float attenuation = saturate(1.0f - distance / max(light.positionRange.w, 1e-4f));
        attenuation *= attenuation;
        if (light.directionType.w > 0.5f)
        {
            float cone = dot(-lightDirection, normalize(light.directionType.xyz));
            attenuation *= smoothstep(light.spotAnglesShadow.y, light.spotAnglesShadow.x, cone);
        }
        color += PbrDirectLighting(albedoSample.rgb, metallic, roughness, normal, viewDirection, lightDirection,
                                   light.colorIntensity.rgb * light.colorIntensity.w * attenuation);
    }
    g_HdrOutput[pixel] = float4(max(color, 0.0f), 1.0f);
}
