StructuredBuffer<float4> g_DDGIMetadata : register(t0);
StructuredBuffer<float> g_SceneSdf : register(t1);
StructuredBuffer<uint> g_SceneVoxels : register(t2);
RWStructuredBuffer<float4> g_DDGIProbeSH2Out : register(u0);

cbuffer DDGIUpdateParams : register(b0)
{
    float4 g_LightDirection;
    float4 g_LightColor;
    float4 g_AmbientInfo;
};

static const float PI = 3.14159265359f;
static const float DDGI_SURFACE_ALBEDO = 0.45f;
static const float DDGI_BOUNCE_SCALE = 0.45f;

struct ClipLevel
{
    float3 minBounds;
    float extent;
    float cellSize;
    float probeCellSize;
    uint sdfOffset;
    uint voxelWordOffset;
    float3 probeOrigin;
    uint probeOffset;
};

ClipLevel LoadLevel(uint level)
{
    uint baseIndex = 1 + level * 3;
    ClipLevel data;
    data.minBounds = g_DDGIMetadata[baseIndex + 0].xyz;
    data.extent = g_DDGIMetadata[baseIndex + 0].w;
    data.cellSize = g_DDGIMetadata[baseIndex + 1].x;
    data.probeCellSize = g_DDGIMetadata[baseIndex + 1].y;
    data.sdfOffset = (uint)g_DDGIMetadata[baseIndex + 1].z;
    data.voxelWordOffset = (uint)g_DDGIMetadata[baseIndex + 1].w;
    data.probeOrigin = g_DDGIMetadata[baseIndex + 2].xyz;
    data.probeOffset = (uint)g_DDGIMetadata[baseIndex + 2].w;
    return data;
}

uint SdfIndex(uint resolution, uint3 cell)
{
    return (cell.z * resolution + cell.y) * resolution + cell.x;
}

float SampleSceneSdf(uint level, float3 worldPos)
{
    uint resolution = (uint)g_DDGIMetadata[0].z;
    ClipLevel clip = LoadLevel(level);
    float3 local = (worldPos - clip.minBounds) / max(clip.extent, 1e-5f);
    if (any(local < 0.0f) || any(local > 1.0f)) {
        return clip.extent;
    }
    float3 grid = saturate(local) * (float)(resolution - 1);
    uint3 c0 = (uint3)floor(grid);
    uint3 c1 = min(c0 + 1, resolution - 1);
    float3 t = frac(grid);

    float c000 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c0.x, c0.y, c0.z))];
    float c100 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c1.x, c0.y, c0.z))];
    float c010 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c0.x, c1.y, c0.z))];
    float c110 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c1.x, c1.y, c0.z))];
    float c001 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c0.x, c0.y, c1.z))];
    float c101 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c1.x, c0.y, c1.z))];
    float c011 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c0.x, c1.y, c1.z))];
    float c111 = g_SceneSdf[clip.sdfOffset + SdfIndex(resolution, uint3(c1.x, c1.y, c1.z))];
    float c00 = lerp(c000, c100, t.x);
    float c10 = lerp(c010, c110, t.x);
    float c01 = lerp(c001, c101, t.x);
    float c11 = lerp(c011, c111, t.x);
    return lerp(lerp(c00, c10, t.y), lerp(c01, c11, t.y), t.z);
}

bool SampleSceneVoxel(uint level, float3 worldPos)
{
    uint resolution = (uint)g_DDGIMetadata[0].z;
    ClipLevel clip = LoadLevel(level);
    float3 local = (worldPos - clip.minBounds) / max(clip.extent, 1e-5f);
    if (any(local < 0.0f) || any(local > 1.0f)) {
        return false;
    }
    uint3 cell = min((uint3)(saturate(local) * (float)resolution), resolution - 1);
    uint index = SdfIndex(resolution, cell);
    uint word = g_SceneVoxels[clip.voxelWordOffset + index / 32u];
    return ((word >> (index & 31u)) & 1u) != 0u;
}

float3 EstimateSceneNormal(uint level, float3 worldPos)
{
    ClipLevel clip = LoadLevel(level);
    float e = max(clip.cellSize, 1e-4f);
    float dx = SampleSceneSdf(level, worldPos + float3(e, 0.0f, 0.0f)) -
        SampleSceneSdf(level, worldPos - float3(e, 0.0f, 0.0f));
    float dy = SampleSceneSdf(level, worldPos + float3(0.0f, e, 0.0f)) -
        SampleSceneSdf(level, worldPos - float3(0.0f, e, 0.0f));
    float dz = SampleSceneSdf(level, worldPos + float3(0.0f, 0.0f, e)) -
        SampleSceneSdf(level, worldPos - float3(0.0f, 0.0f, e));
    return normalize(float3(dx, dy, dz));
}

float3 ProbeSampleDirection(uint index)
{
    if (index == 0u) return normalize(float3(1.0f, 0.0f, 0.0f));
    if (index == 1u) return normalize(float3(-1.0f, 0.0f, 0.0f));
    if (index == 2u) return normalize(float3(0.0f, 1.0f, 0.0f));
    if (index == 3u) return normalize(float3(0.0f, -1.0f, 0.0f));
    if (index == 4u) return normalize(float3(0.0f, 0.0f, 1.0f));
    if (index == 5u) return normalize(float3(0.0f, 0.0f, -1.0f));
    if (index == 6u) return normalize(float3(1.0f, 1.0f, 1.0f));
    if (index == 7u) return normalize(float3(-1.0f, 1.0f, 1.0f));
    if (index == 8u) return normalize(float3(1.0f, -1.0f, 1.0f));
    if (index == 9u) return normalize(float3(1.0f, 1.0f, -1.0f));
    if (index == 10u) return normalize(float3(-1.0f, -1.0f, 1.0f));
    return normalize(float3(-1.0f, 1.0f, -1.0f));
}

bool TraceSurfaceHit(uint level, float3 origin, float3 direction, out float3 hitPos)
{
    ClipLevel clip = LoadLevel(level);
    float t = clip.cellSize * 0.5f;
    float maxDistance = clip.probeCellSize * 2.25f;
    float previousDistance = SampleSceneSdf(level, origin);
    bool startedInside = previousDistance < 0.0f || SampleSceneVoxel(level, origin);
    [loop]
    for (uint stepIndex = 0; stepIndex < 16; ++stepIndex) {
        hitPos = origin + direction * t;
        float d = SampleSceneSdf(level, hitPos);
        bool occupied = SampleSceneVoxel(level, hitPos);
        if (!startedInside && (abs(d) <= clip.cellSize * 1.5f || occupied)) {
            return true;
        }
        if (startedInside && previousDistance < 0.0f && d >= 0.0f) {
            return true;
        }
        previousDistance = d;
        t += max(startedInside ? clip.cellSize : abs(d), clip.cellSize);
        if (t > maxDistance) break;
    }
    hitPos = origin + direction * maxDistance;
    return false;
}

float TraceSunVisibility(uint level, float3 origin, float3 direction)
{
    float visibility = 1.0f;
    ClipLevel clip = LoadLevel(level);
    float t = clip.cellSize * 2.0f;
    [loop]
    for (uint stepIndex = 0; stepIndex < 16; ++stepIndex) {
        float3 p = origin + direction * t;
        float d = SampleSceneSdf(level, p);
        if (d < clip.cellSize * 0.5f || SampleSceneVoxel(level, p)) {
            visibility = 0.0f;
            break;
        }
        t += max(abs(d), clip.cellSize);
        if (t > clip.extent * 0.5f) break;
    }
    return visibility;
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

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID)
{
    uint probeResolution = (uint)g_DDGIMetadata[0].w;
    uint levelCount = (uint)g_DDGIMetadata[0].y;
    uint level = dispatchId.z / probeResolution;
    uint probeZ = dispatchId.z % probeResolution;
    if (g_DDGIMetadata[0].x < 0.5f || level >= levelCount ||
        dispatchId.x >= probeResolution || dispatchId.y >= probeResolution) {
        return;
    }

    ClipLevel clip = LoadLevel(level);
    float3 probePos = clip.probeOrigin + float3(
        ((float)dispatchId.x + 0.5f) * clip.probeCellSize,
        ((float)dispatchId.y + 0.5f) * clip.probeCellSize,
        ((float)probeZ + 0.5f) * clip.probeCellSize);

    uint outputBase = (clip.probeOffset +
        (probeZ * probeResolution * probeResolution) +
        (dispatchId.y * probeResolution + dispatchId.x)) * 9u;
    [unroll]
    for (uint i = 0; i < 9; ++i) {
        g_DDGIProbeSH2Out[outputBase + i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 sunDirection = normalize(-g_LightDirection.xyz);
    float directionalIntensity = max(g_LightDirection.w, 0.0f);
    float3 accumulatedSH[9];
    [unroll]
    for (uint shIndex = 0; shIndex < 9; ++shIndex) {
        accumulatedSH[shIndex] = 0.0f;
    }
    float hitWeight = 0.0f;
    [loop]
    for (uint sampleIndex = 0; sampleIndex < 12u; ++sampleIndex) {
        float3 rayDirection = ProbeSampleDirection(sampleIndex);
        float3 hitPos;
        if (!TraceSurfaceHit(level, probePos, rayDirection, hitPos)) continue;
        float3 normal = EstimateSceneNormal(level, hitPos);
        float3 toProbe = normalize(probePos - hitPos);
        float normalToProbe = dot(normal, toProbe);
        if (normalToProbe < 0.0f) {
            normal = -normal;
            normalToProbe = -normalToProbe;
        }
        float geometryWeight = saturate(normalToProbe);
        float nDotSun = saturate(dot(normal, sunDirection));
        if (geometryWeight <= 0.0f || nDotSun <= 0.0f) continue;
        float visibility = TraceSunVisibility(level, hitPos + normal * clip.cellSize * 2.0f, sunDirection);
        float3 surfaceBounce = g_LightColor.rgb * directionalIntensity *
            nDotSun * visibility * geometryWeight *
            (DDGI_SURFACE_ALBEDO * DDGI_BOUNCE_SCALE / PI);
        float sh[9];
        EvalSH2(rayDirection, sh);
        [unroll]
        for (uint coefficientIndex = 0; coefficientIndex < 9; ++coefficientIndex) {
            accumulatedSH[coefficientIndex] += surfaceBounce * sh[coefficientIndex];
        }
        hitWeight += geometryWeight;
    }

    if (hitWeight <= 1e-4f) {
        return;
    }
    static const float kSHL0 = 0.282095f;
    float coefficientScale = 1.0f / (kSHL0 * kSHL0 * max(hitWeight, 1e-4f));
    float validity = saturate(hitWeight / 2.0f);
    [unroll]
    for (uint outputIndex = 0; outputIndex < 9; ++outputIndex) {
        g_DDGIProbeSH2Out[outputBase + outputIndex] =
            float4(accumulatedSH[outputIndex] * coefficientScale, outputIndex == 0u ? validity : 0.0f);
    }
}
