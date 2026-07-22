#ifndef MYENGINE_MODERN_RAY_TRACING_HLSLI
#define MYENGINE_MODERN_RAY_TRACING_HLSLI

#include "PBR_BRDF.hlsli"
#include "ProbeLighting.hlsli"

struct ModernRTVertex
{
    float3 position;
    float3 normal;
    float3 tangent;
    float2 uv0;
    float2 uv1;
    float4 color;
    float4 boneIndices;
    float4 boneWeights;
};

struct ModernRTObject
{
    row_major float4x4 world;
    row_major float4x4 previousWorld;
    row_major float4x4 normalMatrix;
    float4 boundsMin;
    float4 boundsMax;
    uint meshId;
    uint materialId;
    uint bonePaletteOffset;
    uint flags;
    uint firstIndex;
    uint indexCount;
    int baseVertex;
    uint objectId;
};

struct ModernRTMaterial
{
    float4 baseColor;
    float4 material;
    float4 emissive;
    uint4 textureIndices0;
    uint4 samplerIndices0;
    uint textureIndex4;
    uint samplerIndex4;
    uint flags;
    uint padding;
};

cbuffer ModernRayTracingConstants : register(b0)
{
    row_major float4x4 g_RTInverseViewProjection;
    float4 g_RTCameraPositionAmbient;
    float4 g_RTLightDirectionIntensity;
    float4 g_RTLightColor;
    float4 g_RTEnvironmentColor;
    uint2 g_RTFullSize;
    uint2 g_RTEffectSize;
    float4 g_RTParams0; // AO radius, bias, power, intensity
    float4 g_RTParams1; // max distance, max roughness, shadow intensity, frame index
    uint g_RTLocalReflectionProbeCount;
    uint g_RTLocalSHProbeVolumeCount;
    float g_RTLocalReflectionMipCount;
    float g_RTProbeLightingPadding;
};

RaytracingAccelerationStructure g_RTScene : register(t0);
Texture2D<float> g_RTDepth : register(t1);
Texture2D<float4> g_RTNormal : register(t2);
Texture2D<float4> g_RTAlbedo : register(t3);
Texture2D<float4> g_RTMaterialBuffer : register(t4);
TextureCube<float4> g_RTEnvironment : register(t5);
StructuredBuffer<ModernRTVertex> g_RTVertices : register(t6);
StructuredBuffer<uint> g_RTIndices : register(t7);
StructuredBuffer<ModernRTObject> g_RTObjects : register(t8);
StructuredBuffer<ModernRTMaterial> g_RTMaterials : register(t9);
StructuredBuffer<float4> g_RTEnvironmentSH2 : register(t10);
Texture2DArray<float4> g_RTLocalReflectionProbes : register(t11);
StructuredBuffer<ReflectionProbeGpuData> g_RTLocalReflectionProbeData : register(t12);
StructuredBuffer<SHProbeVolumeGpuData> g_RTLocalSHProbeVolumes : register(t13);
StructuredBuffer<float4> g_RTLocalSHCoefficients : register(t14);
Texture2D<float4> g_RTBindlessTextures[] : register(t0, space1);
SamplerState g_LinearRepeatSampler : register(s0);
SamplerState g_PointRepeatSampler : register(s1);
SamplerState g_LinearClampURepeatVSampler : register(s2);
SamplerState g_PointClampURepeatVSampler : register(s3);
SamplerState g_LinearRepeatUClampVSampler : register(s4);
SamplerState g_PointRepeatUClampVSampler : register(s5);
SamplerState g_LinearClampSampler : register(s6);
SamplerState g_PointClampSampler : register(s7);
SamplerState g_RTLinearSampler : register(s8);

static const uint MODERN_RT_ALPHA_TEST_BIT = 1u << 8u;
static const uint MODERN_RT_BASE_COLOR_TEXTURE_BIT = 1u << 0u;
static const uint MODERN_RT_METALLIC_ROUGHNESS_TEXTURE_BIT = 1u << 2u;
static const uint MODERN_RT_OCCLUSION_TEXTURE_BIT = 1u << 3u;
static const uint MODERN_RT_EMISSIVE_TEXTURE_BIT = 1u << 4u;

float4 ModernRTSampleGiMaterial(uint textureIndex, uint samplerIndex, float2 uv)
{
    switch (samplerIndex)
    {
    case 1u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_PointRepeatSampler, uv, 0.0f);
    case 2u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_LinearClampURepeatVSampler, uv, 0.0f);
    case 3u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_PointClampURepeatVSampler, uv, 0.0f);
    case 4u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_LinearRepeatUClampVSampler, uv, 0.0f);
    case 5u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_PointRepeatUClampVSampler, uv, 0.0f);
    case 6u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_LinearClampSampler, uv, 0.0f);
    case 7u:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_PointClampSampler, uv, 0.0f);
    default:
        return g_RTBindlessTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(g_LinearRepeatSampler, uv, 0.0f);
    }
}

uint ModernRTHash32(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float ModernRTUnitFloat(uint value)
{
    // Retain the high 24 bits so conversion to float cannot round UINT_MAX to 1.0.
    return float(value >> 8u) * (1.0f / 16777216.0f);
}

float2 ModernRTSample2D(uint2 pixel, uint frameIndex, uint stream)
{
    // A stable per-pixel scramble combined with an R2 additive recurrence gives every pixel a low-discrepancy
    // temporal sequence. Hash(pixel + frameIndex) must not be used here: it makes frame N+1 at pixel P identical to
    // frame N at P+(1,1), translating the complete noise field toward the upper-left every frame.
    const uint pixelKey = ModernRTHash32(pixel.x ^ (pixel.y * 0x9e3779b9u) ^ (stream * 0x85ebca6bu));
    const float2 scramble =
        float2(ModernRTUnitFloat(ModernRTHash32(pixelKey ^ 0x68bc21ebu)),
               ModernRTUnitFloat(ModernRTHash32(pixelKey ^ 0x02e5be93u)));
    const float sampleNumber = float(frameIndex + 1u);
    return frac(scramble + sampleNumber * float2(0.754877666f, 0.569840291f));
}

float3 ModernRTDecodeNormal(float3 encoded)
{
    float3 value = encoded * 2.0f - 1.0f;
    return normalize(value);
}

uint2 ModernRTEffectPixelToFullPixel(uint2 pixel)
{
    float2 position = (float2(pixel) + 0.5f) * float2(g_RTFullSize) / float2(g_RTEffectSize);
    return min((uint2)position, g_RTFullSize - 1u);
}

float3 ModernRTReconstructWorldPosition(uint2 pixel, float depth)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(g_RTFullSize);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 world = mul(float4(ndc, depth, 1.0f), g_RTInverseViewProjection);
    return world.xyz / max(abs(world.w), 1e-6f);
}

void ModernRTTriangle(uint instanceId, uint primitiveIndex, float2 barycentrics, out ModernRTObject object,
                      out ModernRTMaterial material, out float2 uv, out float4 vertexColor, out float3 normal,
                      out float3 worldPosition)
{
    object = g_RTObjects[instanceId];
    material = g_RTMaterials[object.materialId];
    uint triangle = object.firstIndex + primitiveIndex * 3u;
    uint3 indices = uint3(g_RTIndices[triangle], g_RTIndices[triangle + 1u], g_RTIndices[triangle + 2u]);
    ModernRTVertex v0 = g_RTVertices[indices.x];
    ModernRTVertex v1 = g_RTVertices[indices.y];
    ModernRTVertex v2 = g_RTVertices[indices.z];
    float3 bary = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    uv = v0.uv0 * bary.x + v1.uv0 * bary.y + v2.uv0 * bary.z;
    vertexColor = v0.color * bary.x + v1.color * bary.y + v2.color * bary.z;
    float3 localNormal = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
    float3 localPosition = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    normal = normalize(mul(float4(localNormal, 0.0f), object.normalMatrix).xyz);
    worldPosition = mul(float4(localPosition, 1.0f), object.world).xyz;
}

bool ModernRTAcceptCandidate(inout RayQuery<RAY_FLAG_NONE> query)
{
    uint instanceId = query.CandidateInstanceID();
    ModernRTObject object = g_RTObjects[instanceId];
    ModernRTMaterial material = g_RTMaterials[object.materialId];
    if ((material.flags & MODERN_RT_ALPHA_TEST_BIT) == 0u)
        return true;
    uint primitiveIndex = query.CandidatePrimitiveIndex();
    float2 barycentrics = query.CandidateTriangleBarycentrics();
    float2 uv;
    float4 vertexColor;
    float3 normal;
    float3 worldPosition;
    ModernRTObject ignoredObject;
    ModernRTMaterial ignoredMaterial;
    ModernRTTriangle(instanceId, primitiveIndex, barycentrics, ignoredObject, ignoredMaterial, uv, vertexColor, normal,
                     worldPosition);
    float alpha = material.baseColor.a * vertexColor.a;
    if ((material.flags & MODERN_RT_BASE_COLOR_TEXTURE_BIT) != 0u && material.textureIndices0.x != 0xffffffffu)
        alpha *= g_RTBindlessTextures[NonUniformResourceIndex(material.textureIndices0.x)]
                     .SampleLevel(g_RTLinearSampler, uv, 0.0f)
                     .a;
    return alpha >= material.material.w;
}

bool ModernRTTrace(float3 origin, float3 direction, float minimumDistance, float maximumDistance,
                   out uint instanceId, out uint primitiveIndex, out float2 barycentrics, out float hitDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = minimumDistance;
    ray.TMax = maximumDistance;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_RTScene, RAY_FLAG_NONE, 0xffu, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && ModernRTAcceptCandidate(query))
            query.CommitNonOpaqueTriangleHit();
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        instanceId = primitiveIndex = 0u;
        barycentrics = 0.0f;
        hitDistance = maximumDistance;
        return false;
    }
    instanceId = query.CommittedInstanceID();
    primitiveIndex = query.CommittedPrimitiveIndex();
    barycentrics = query.CommittedTriangleBarycentrics();
    hitDistance = query.CommittedRayT();
    return true;
}

bool ModernRTVisible(float3 origin, float3 direction, float maximumDistance)
{
    uint instanceId;
    uint primitiveIndex;
    float2 barycentrics;
    float hitDistance;
    return !ModernRTTrace(origin, direction, 0.001f, maximumDistance, instanceId, primitiveIndex, barycentrics,
                          hitDistance);
}

float3 ModernRTCosineDirection(float3 normal, float2 randomValue)
{
    float phi = 6.28318530718f * randomValue.x;
    float radius = sqrt(randomValue.y);
    float z = sqrt(max(1.0f - randomValue.y, 0.0f));
    float3 tangent = normalize(abs(normal.y) < 0.999f ? cross(float3(0.0f, 1.0f, 0.0f), normal)
                                                       : cross(float3(1.0f, 0.0f, 0.0f), normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (cos(phi) * radius) + bitangent * (sin(phi) * radius) + normal * z);
}

float3 ModernRTGGXReflectionDirection(float3 normal, float3 viewDirection, float roughness, float2 randomValue)
{
    const float alpha = max(roughness * roughness, 0.0025f);
    const float alphaSquared = alpha * alpha;
    const float phi = 6.28318530718f * randomValue.x;
    const float cosineTheta = sqrt(saturate((1.0f - randomValue.y) /
                                            (1.0f + (alphaSquared - 1.0f) * randomValue.y)));
    const float sineTheta = sqrt(max(1.0f - cosineTheta * cosineTheta, 0.0f));
    const float3 tangent = normalize(abs(normal.y) < 0.999f
                                         ? cross(float3(0.0f, 1.0f, 0.0f), normal)
                                         : cross(float3(1.0f, 0.0f, 0.0f), normal));
    const float3 bitangent = cross(normal, tangent);
    const float3 halfVector = normalize(tangent * (cos(phi) * sineTheta) +
                                        bitangent * (sin(phi) * sineTheta) + normal * cosineTheta);
    const float3 reflected = normalize(reflect(-viewDirection, halfVector));
    return dot(reflected, normal) > 0.0f ? reflected : normalize(reflect(-viewDirection, normal));
}

float3 ModernRTSurfaceRadiance(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    ModernRTObject object;
    ModernRTMaterial material;
    float2 uv;
    float4 vertexColor;
    float3 normal;
    float3 worldPosition;
    ModernRTTriangle(instanceId, primitiveIndex, barycentrics, object, material, uv, vertexColor, normal,
                     worldPosition);
    float3 baseColor = material.baseColor.rgb;
    if ((material.flags & MODERN_RT_BASE_COLOR_TEXTURE_BIT) != 0u && material.textureIndices0.x != 0xffffffffu)
        baseColor *= g_RTBindlessTextures[NonUniformResourceIndex(material.textureIndices0.x)]
                         .SampleLevel(g_RTLinearSampler, uv, 0.0f)
                         .rgb;
    float3 sunDirection = normalize(-g_RTLightDirectionIntensity.xyz);
    float nDotL = saturate(dot(normal, sunDirection));
    float visibility = nDotL > 0.0f ? (ModernRTVisible(worldPosition + normal * 0.002f, sunDirection, 10000.0f)
                                           ? 1.0f
                                           : 0.0f)
                                    : 0.0f;
    float3 environment = g_RTEnvironment.SampleLevel(g_RTLinearSampler, normal, 6.0f).rgb;
    return material.emissive.rgb + baseColor * (environment * g_RTCameraPositionAmbient.w *
                                                  max(g_RTEnvironmentColor.rgb, 0.0f) +
                                                  g_RTLightColor.rgb * g_RTLightDirectionIntensity.w * nDotL * visibility);
}

float3 ModernRTSurfaceGiRadiance(uint instanceId, uint primitiveIndex, float2 barycentrics, float3 viewDirection)
{
    ModernRTObject object;
    ModernRTMaterial material;
    float2 uv;
    float4 vertexColor;
    float3 normal;
    float3 worldPosition;
    ModernRTTriangle(instanceId, primitiveIndex, barycentrics, object, material, uv, vertexColor, normal,
                     worldPosition);

    viewDirection = normalize(viewDirection);
    if (dot(normal, viewDirection) < 0.0f)
        normal = -normal;

    float4 baseSample = 1.0f;
    if ((material.flags & MODERN_RT_BASE_COLOR_TEXTURE_BIT) != 0u && material.textureIndices0.x != 0xffffffffu)
        baseSample = ModernRTSampleGiMaterial(material.textureIndices0.x, material.samplerIndices0.x, uv);
    const float3 baseColor = pow(max(baseSample.rgb * material.baseColor.rgb * vertexColor.rgb, 0.0f), 2.2f);

    float metallic = material.material.x;
    float roughness = material.material.y;
    float ao = material.material.z;
    if ((material.flags & MODERN_RT_METALLIC_ROUGHNESS_TEXTURE_BIT) != 0u &&
        material.textureIndices0.z != 0xffffffffu)
    {
        const float4 metallicRoughness =
            ModernRTSampleGiMaterial(material.textureIndices0.z, material.samplerIndices0.z, uv);
        metallic *= metallicRoughness.b;
        roughness *= metallicRoughness.g;
    }
    if ((material.flags & MODERN_RT_OCCLUSION_TEXTURE_BIT) != 0u && material.textureIndices0.w != 0xffffffffu)
        ao *= ModernRTSampleGiMaterial(material.textureIndices0.w, material.samplerIndices0.w, uv).r;
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.04f, 1.0f);
    ao = max(ao, 0.0f);

    float3 emissive = material.emissive.rgb;
    if ((material.flags & MODERN_RT_EMISSIVE_TEXTURE_BIT) != 0u && material.textureIndex4 != 0xffffffffu)
        emissive *= pow(max(ModernRTSampleGiMaterial(material.textureIndex4, material.samplerIndex4, uv).rgb, 0.0f),
                        2.2f);

    const float3 sunDirection = normalize(-g_RTLightDirectionIntensity.xyz);
    const float nDotL = saturate(dot(normal, sunDirection));
    const float visibility =
        nDotL > 0.0f && ModernRTVisible(worldPosition + normal * 0.002f, sunDirection, 10000.0f) ? 1.0f : 0.0f;
    float3 radiance = PbrDirectLighting(baseColor, metallic, roughness, normal, viewDirection, sunDirection,
                                        g_RTLightColor.rgb * max(g_RTLightDirectionIntensity.w, 0.0f) * visibility);

    const float3 globalEnvironmentDiffuse = EvaluateProbeSHBasis(g_RTEnvironmentSH2, 0u, normal);
    const float3 environmentDiffuse = EvaluateLocalSHVolumes(
        g_RTLocalSHProbeVolumes, g_RTLocalSHCoefficients, g_RTLocalSHProbeVolumeCount, worldPosition, normal,
        globalEnvironmentDiffuse);
    const float3 reflectionDirection = normalize(reflect(-viewDirection, normal));
    const float3 globalEnvironmentSpecular =
        g_RTEnvironment.SampleLevel(g_RTLinearSampler, reflectionDirection, roughness * 6.0f).rgb;
    const float3 environmentSpecular = SampleLocalReflectionsAuto(
        g_RTLocalReflectionProbes, g_RTLinearSampler, g_RTLocalReflectionProbeData,
        g_RTLocalReflectionProbeCount, worldPosition, reflectionDirection, roughness, g_RTLocalReflectionMipCount,
        globalEnvironmentSpecular);
    radiance += PbrEnvironmentLighting(baseColor, metallic, roughness, ao, normal, viewDirection,
                                       environmentDiffuse, environmentSpecular) *
                max(g_RTLightColor.w, 0.0f) * max(g_RTEnvironmentColor.rgb, 0.0f);
    return max(radiance + emissive, 0.0f);
}

#endif
