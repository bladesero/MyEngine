#ifndef MYENGINE_PROBE_LIGHTING_HLSLI
#define MYENGINE_PROBE_LIGHTING_HLSLI

struct ReflectionProbeGpuData
{
    row_major float4x4 worldToLocal;
    row_major float4x4 localToWorld;
    float4 extentsIntensity;
    float4 positionRange;
    uint4 layerPriority;
    float4 blendInfo;
};

struct SHProbeVolumeGpuData
{
    row_major float4x4 worldToLocal;
    float4 extentsIntensity;
    float4 blendPriority;
    uint4 gridAndOffset;
};

float2 ProbeDirectionToOctaUv(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 1e-6f);
    float2 uv = direction.xy;
    if (direction.z < 0.0f)
        uv = (1.0f - abs(uv.yx)) * float2(uv.x >= 0.0f ? 1.0f : -1.0f,
                                           uv.y >= 0.0f ? 1.0f : -1.0f);
    return uv * 0.5f + 0.5f;
}

float ProbeInfluenceWeight(float3 localPosition, float3 extents, float blendDistance)
{
    float3 distanceToEdge = extents - abs(localPosition);
    if (any(distanceToEdge < 0.0f))
        return 0.0f;
    return blendDistance > 1e-4f ? saturate(min(distanceToEdge.x, min(distanceToEdge.y, distanceToEdge.z)) /
                                            blendDistance)
                                  : 1.0f;
}

float3 ProbeBoxProjectedDirection(float3 worldPosition, float3 reflectionDirection, ReflectionProbeGpuData probe)
{
    float3 localPosition = mul(float4(worldPosition, 1.0f), probe.worldToLocal).xyz;
    float3 localDirection = mul(float4(reflectionDirection, 0.0f), probe.worldToLocal).xyz;
    float3 safeDirection = sign(localDirection) * max(abs(localDirection), 1e-5f);
    float3 t0 = (-probe.extentsIntensity.xyz - localPosition) / safeDirection;
    float3 t1 = ( probe.extentsIntensity.xyz - localPosition) / safeDirection;
    float3 tFar = max(t0, t1);
    float distance = min(tFar.x, min(tFar.y, tFar.z));
    float3 localHit = localPosition + localDirection * max(distance, 0.0f);
    float3 worldHit = mul(float4(localHit, 1.0f), probe.localToWorld).xyz;
    return normalize(worldHit - probe.positionRange.xyz);
}

float3 SampleLinearProbeReflection(Texture2DArray<float4> atlas, SamplerState atlasSampler,
                                   ReflectionProbeGpuData probe, float3 worldPosition,
                                   float3 reflectionDirection, float roughness, float mipCount)
{
    float3 projected = ProbeBoxProjectedDirection(worldPosition, reflectionDirection, probe);
    uint width, height, layers, levels;
    atlas.GetDimensions(0, width, height, layers, levels);
    float2 halfTexel = 0.5f / max(float2(width, height), 1.0f);
    float2 uv = clamp(ProbeDirectionToOctaUv(projected), halfTexel, 1.0f - halfTexel);
    float3 radiance = atlas.SampleLevel(atlasSampler,
        float3(uv, probe.layerPriority.x),
        saturate(roughness) * max(mipCount - 1.0f, 0.0f)).rgb;
    return max(radiance, 0.0f) * probe.extentsIntensity.w;
}

float3 EvaluateProbeSHBasis(StructuredBuffer<float4> coefficients, uint offset, float3 direction)
{
    float basis[9];
    direction = normalize(direction);
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * direction.y;
    basis[2] = 0.488603f * direction.z;
    basis[3] = 0.488603f * direction.x;
    basis[4] = 1.092548f * direction.x * direction.y;
    basis[5] = 1.092548f * direction.y * direction.z;
    basis[6] = 0.315392f * (3.0f * direction.z * direction.z - 1.0f);
    basis[7] = 1.092548f * direction.x * direction.z;
    basis[8] = 0.546274f * (direction.x * direction.x - direction.y * direction.y);
    float3 color = 0.0f;
    [unroll]
    for (uint i = 0; i < 9; ++i)
        color += coefficients[offset + i].rgb * basis[i];
    return max(color, 0.0f);
}

float3 SampleSHGrid(StructuredBuffer<float4> coefficients, SHProbeVolumeGpuData volume,
                    float3 localPosition, float3 direction)
{
    uint3 dimensions = volume.gridAndOffset.xyz;
    float3 uvw = saturate(localPosition / max(volume.extentsIntensity.xyz, 1e-4f) * 0.5f + 0.5f);
    float3 grid = uvw * float3(max(dimensions.x, 1u) - 1u, max(dimensions.y, 1u) - 1u,
                              max(dimensions.z, 1u) - 1u);
    uint3 cell0 = min((uint3)floor(grid), dimensions - 1u);
    uint3 cell1 = min(cell0 + 1u, dimensions - 1u);
    float3 fraction = frac(grid);
    float3 result = 0.0f;
    [unroll]
    for (uint z = 0; z < 2; ++z) {
        [unroll]
        for (uint y = 0; y < 2; ++y) {
            [unroll]
            for (uint x = 0; x < 2; ++x) {
                uint3 cell = uint3(x != 0u ? cell1.x : cell0.x, y != 0u ? cell1.y : cell0.y,
                                   z != 0u ? cell1.z : cell0.z);
                uint sampleIndex = (cell.z * dimensions.y + cell.y) * dimensions.x + cell.x;
                float3 weights = float3(x != 0u ? fraction.x : 1.0f - fraction.x,
                                        y != 0u ? fraction.y : 1.0f - fraction.y,
                                        z != 0u ? fraction.z : 1.0f - fraction.z);
                result += EvaluateProbeSHBasis(coefficients,
                    volume.gridAndOffset.w + sampleIndex * 9u, direction) * weights.x * weights.y * weights.z;
            }
        }
    }
    return result * volume.extentsIntensity.w;
}

float3 EvaluateLocalSHVolumes(StructuredBuffer<SHProbeVolumeGpuData> volumes,
                              StructuredBuffer<float4> coefficients, uint volumeCount,
                              float3 worldPosition, float3 direction, float3 fallback)
{
    float highestPriority = -3.4e38f;
    [loop]
    for (uint volumeIndexA = 0; volumeIndexA < min(volumeCount, 8u); ++volumeIndexA) {
        float3 local = mul(float4(worldPosition, 1.0f), volumes[volumeIndexA].worldToLocal).xyz;
        if (ProbeInfluenceWeight(local, volumes[volumeIndexA].extentsIntensity.xyz,
                                 volumes[volumeIndexA].blendPriority.x) > 0.0f)
            highestPriority = max(highestPriority, volumes[volumeIndexA].blendPriority.y);
    }
    float3 sum = 0.0f;
    float weightSum = 0.0f;
    [loop]
    for (uint volumeIndexB = 0; volumeIndexB < min(volumeCount, 8u); ++volumeIndexB) {
        SHProbeVolumeGpuData volume = volumes[volumeIndexB];
        float3 local = mul(float4(worldPosition, 1.0f), volume.worldToLocal).xyz;
        float weight = ProbeInfluenceWeight(local, volume.extentsIntensity.xyz, volume.blendPriority.x);
        if (weight > 0.0f && volume.blendPriority.y >= highestPriority) {
            sum += SampleSHGrid(coefficients, volume, local, direction) * weight;
            weightSum += weight;
        }
    }
    return weightSum > 0.0f ? sum / weightSum : fallback;
}

float3 SampleLocalReflectionsAuto(Texture2DArray<float4> atlas, SamplerState atlasSampler,
                                  StructuredBuffer<ReflectionProbeGpuData> probes, uint probeCount,
                                  float3 worldPosition, float3 reflectionDirection, float roughness,
                                  float mipCount, float3 fallback)
{
    uint best0 = 0xffffffffu, best1 = 0xffffffffu;
    float weight0 = 0.0f, weight1 = 0.0f;
    float priority0 = -3.4e38f, priority1 = -3.4e38f;
    [loop]
    for (uint i = 0; i < min(probeCount, 32u); ++i) {
        float3 local = mul(float4(worldPosition, 1.0f), probes[i].worldToLocal).xyz;
        float weight = ProbeInfluenceWeight(local, probes[i].extentsIntensity.xyz, probes[i].blendInfo.x);
        float priority = probes[i].blendInfo.y;
        if (weight > 0.0f && (best0 == 0xffffffffu || priority > priority0 ||
                             (priority == priority0 && weight > weight0))) {
            best1 = best0; weight1 = weight0; priority1 = priority0;
            best0 = i; weight0 = weight; priority0 = priority;
        } else if (weight > 0.0f && (best1 == 0xffffffffu || priority > priority1 ||
                                    (priority == priority1 && weight > weight1))) {
            best1 = i; weight1 = weight; priority1 = priority;
        }
    }
    if (best0 == 0xffffffffu)
        return fallback;
    float3 first = SampleLinearProbeReflection(atlas, atlasSampler, probes[best0], worldPosition,
                                               reflectionDirection, roughness, mipCount);
    if (best1 == 0xffffffffu || priority1 != priority0)
        return first;
    float3 second = SampleLinearProbeReflection(atlas, atlasSampler, probes[best1], worldPosition,
                                                reflectionDirection, roughness, mipCount);
    return lerp(first, second, weight1 / max(weight0 + weight1, 1e-5f));
}

#endif
