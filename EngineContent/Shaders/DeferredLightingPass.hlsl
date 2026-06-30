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
StructuredBuffer<float4> g_DDGIProbeSH2 : register(t10);
StructuredBuffer<float4> g_DDGIMetadata : register(t11);

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
    float4 g_DDGIInfo;
    float4 g_ScreenSize;
};

#include "PBR_BRDF.hlsli"
#include "EnvironmentRadiance.hlsli"

static const float DIRECT_SHADOW_MIN_VISIBILITY = 0.03f;

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

struct DDGIClipLevel
{
    float3 minBounds;
    float extent;
    float probeCellSize;
    uint probeOffset;
};

DDGIClipLevel LoadDDGILevel(uint level)
{
    uint baseIndex = 1 + level * 3;
    DDGIClipLevel data;
    data.minBounds = g_DDGIMetadata[baseIndex + 0].xyz;
    data.extent = g_DDGIMetadata[baseIndex + 0].w;
    data.probeCellSize = g_DDGIMetadata[baseIndex + 1].y;
    data.probeOffset = (uint)g_DDGIMetadata[baseIndex + 2].w;
    return data;
}

float3 EvaluateDDGIProbeSH(uint probeBase, float3 direction, out float validWeight)
{
    validWeight = saturate(g_DDGIProbeSH2[probeBase + 0].w);
    if (validWeight <= 1e-4f) {
        validWeight = 0.0f;
        return 0.0f;
    }
    float basis[9];
    EvalSH2(direction, basis);
    float3 color = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i) {
        color += g_DDGIProbeSH2[probeBase + i].rgb * basis[i];
    }
    return max(color, 0.0f);
}

uint DDGIProbeBase(DDGIClipLevel clip, uint probeResolution, uint3 coord)
{
    uint strideY = probeResolution;
    uint strideZ = probeResolution * probeResolution;
    return (clip.probeOffset + coord.z * strideZ + coord.y * strideY + coord.x) * 9u;
}

float4 EvaluateDDGINeighborhood(DDGIClipLevel clip, uint probeResolution,
                                uint3 centerCoord, float3 normal)
{
    float3 irradiance = 0.0f;
    float totalWeight = 0.0f;
    [loop]
    for (int z = -1; z <= 1; ++z) {
        [loop]
        for (int y = -1; y <= 1; ++y) {
            [loop]
            for (int x = -1; x <= 1; ++x) {
                int3 offset = int3(x, y, z);
                int3 probeCoord = clamp(
                    (int3)centerCoord + offset,
                    int3(0, 0, 0),
                    int3((int)probeResolution - 1,
                         (int)probeResolution - 1,
                         (int)probeResolution - 1));
                float validWeight;
                float3 sampleIrradiance = EvaluateDDGIProbeSH(
                    DDGIProbeBase(clip, probeResolution, (uint3)probeCoord),
                    normal, validWeight);
                float distanceWeight = 1.0f /
                    (1.0f + (float)(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z));
                float weight = validWeight * distanceWeight;
                irradiance += sampleIrradiance * weight;
                totalWeight += weight;
            }
        }
    }
    if (totalWeight <= 1e-4f) {
        return 0.0f;
    }
    return float4(max(irradiance / totalWeight, 0.0f), saturate(totalWeight));
}

float4 EvaluateDDGILevel(DDGIClipLevel clipData, uint probeResolution,
                         float3 worldPos, float3 normal)
{
    float3 probeGrid = saturate((worldPos - clipData.minBounds) /
        max(clipData.extent, 1e-5f)) * (float)(probeResolution - 1);
    uint3 p0 = (uint3)floor(probeGrid);
    uint3 p1 = min(p0 + 1, probeResolution - 1);
    float3 t = frac(probeGrid);

    uint b000 = DDGIProbeBase(clipData, probeResolution, uint3(p0.x, p0.y, p0.z));
    uint b100 = DDGIProbeBase(clipData, probeResolution, uint3(p1.x, p0.y, p0.z));
    uint b010 = DDGIProbeBase(clipData, probeResolution, uint3(p0.x, p1.y, p0.z));
    uint b110 = DDGIProbeBase(clipData, probeResolution, uint3(p1.x, p1.y, p0.z));
    uint b001 = DDGIProbeBase(clipData, probeResolution, uint3(p0.x, p0.y, p1.z));
    uint b101 = DDGIProbeBase(clipData, probeResolution, uint3(p1.x, p0.y, p1.z));
    uint b011 = DDGIProbeBase(clipData, probeResolution, uint3(p0.x, p1.y, p1.z));
    uint b111 = DDGIProbeBase(clipData, probeResolution, uint3(p1.x, p1.y, p1.z));
    float w000;
    float w100;
    float w010;
    float w110;
    float w001;
    float w101;
    float w011;
    float w111;
    float3 c000 = EvaluateDDGIProbeSH(b000, normal, w000);
    float3 c100 = EvaluateDDGIProbeSH(b100, normal, w100);
    float3 c010 = EvaluateDDGIProbeSH(b010, normal, w010);
    float3 c110 = EvaluateDDGIProbeSH(b110, normal, w110);
    float3 c001 = EvaluateDDGIProbeSH(b001, normal, w001);
    float3 c101 = EvaluateDDGIProbeSH(b101, normal, w101);
    float3 c011 = EvaluateDDGIProbeSH(b011, normal, w011);
    float3 c111 = EvaluateDDGIProbeSH(b111, normal, w111);

    float3 cornerWeights0 = 1.0f - t;
    float3 cornerWeights1 = t;
    float tw000 = cornerWeights0.x * cornerWeights0.y * cornerWeights0.z * w000;
    float tw100 = cornerWeights1.x * cornerWeights0.y * cornerWeights0.z * w100;
    float tw010 = cornerWeights0.x * cornerWeights1.y * cornerWeights0.z * w010;
    float tw110 = cornerWeights1.x * cornerWeights1.y * cornerWeights0.z * w110;
    float tw001 = cornerWeights0.x * cornerWeights0.y * cornerWeights1.z * w001;
    float tw101 = cornerWeights1.x * cornerWeights0.y * cornerWeights1.z * w101;
    float tw011 = cornerWeights0.x * cornerWeights1.y * cornerWeights1.z * w011;
    float tw111 = cornerWeights1.x * cornerWeights1.y * cornerWeights1.z * w111;
    float totalWeight = tw000 + tw100 + tw010 + tw110 + tw001 + tw101 + tw011 + tw111;
    if (totalWeight <= 1e-4f) {
        return EvaluateDDGINeighborhood(clipData, probeResolution, min(p0 + (uint3)round(t), probeResolution - 1), normal);
    }

    float3 irradiance =
        c000 * tw000 + c100 * tw100 + c010 * tw010 + c110 * tw110 +
        c001 * tw001 + c101 * tw101 + c011 * tw011 + c111 * tw111;
    return float4(max(irradiance / totalWeight, 0.0f), saturate(totalWeight));
}

float4 EvaluateDDGI(float3 worldPos, float3 normal)
{
    if (g_DDGIInfo.x < 0.5f || g_DDGIMetadata[0].x < 0.5f) {
        return 0.0f;
    }
    uint levelCount = (uint)g_DDGIMetadata[0].y;
    uint probeResolution = (uint)g_DDGIMetadata[0].w;
    float4 bestSample = 0.0f;
    [loop]
    for (uint level = 0; level < levelCount; ++level) {
        DDGIClipLevel clip = LoadDDGILevel(level);
        float3 local = (worldPos - clip.minBounds) / max(clip.extent, 1e-5f);
        if (!all(local >= 0.0f) || !all(local <= 1.0f)) {
            continue;
        }
        float4 levelSample = EvaluateDDGILevel(clip, probeResolution, worldPos, normal);
        if (levelSample.w > bestSample.w) {
            bestSample = levelSample;
        }
    }
    return bestSample;
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
    return lerp(DIRECT_SHADOW_MIN_VISIBILITY, 1.0f, shadow / 9.0f);
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
        return float4(EnvironmentRadiance(viewDir, sunDirection), 1.0f);
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
    float4 ddgiIrradiance = EvaluateDDGI(worldPos, N);
    float3 ddgiIrradianceLimit = max(
        g_LightColor.rgb * max(g_LightDirection.w, 0.0f) * 0.35f,
        float3(0.25f, 0.25f, 0.25f));
    ddgiIrradiance.rgb = min(ddgiIrradiance.rgb, ddgiIrradianceLimit);
    if (g_DDGIInfo.y > 0.5f && g_DDGIInfo.y < 1.5f) {
        return float4(ddgiIrradiance.rgb, 1.0f);
    }
    if (g_DDGIInfo.y >= 1.5f) {
        float valid = ddgiIrradiance.w;
        return float4(valid, valid, valid, 1.0f);
    }

    float3 analyticalDiffuse = EnvironmentRadiance(N, sunDirection) / ENV_PI;
    float3 irradiance = (g_DDGIInfo.x > 0.5f)
        ? ddgiIrradiance.rgb
        : ((g_IBLInfo.x > 0.5f) ? EvaluateEnvironmentSH2(N) : analyticalDiffuse);
    float3 prefilteredColor = (g_IBLInfo.x > 0.5f)
        ? g_IBLCubemap.SampleLevel(g_LinearSampler, reflectionDirection, roughness * 6.0f).rgb
        : EnvironmentRadiance(
            normalize(lerp(reflectionDirection, N, roughness * roughness)), sunDirection);
    float iblScale = (g_IBLInfo.x > 0.5f) ? max(g_IBLInfo.y, 0.0f) : 1.0f;
    float3 ambient = PbrEnvironmentLighting(
        albedo, metallic, roughness, ao, N, V, irradiance, prefilteredColor) *
        max(g_LightInfo.y, 0.0f) * iblScale;

    return float4(ambient + direct + emissive, albedoAlpha.a);
}
