cbuffer ScreenSpaceConstants : register(b0)
{
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    row_major float4x4 g_InverseProjection;
    row_major float4x4 g_InverseViewProjection;
    row_major float4x4 g_PreviousInverseViewProjection;
    row_major float4x4 g_PreviousViewProjection;
    float4 g_CameraPositionAmbient;
    float4 g_PreviousCameraPosition;
    uint2 g_FullSize;
    uint2 g_EffectSize;
    float2 g_FullTexelSize;
    float2 g_EffectTexelSize;
    float2 g_CurrentJitterUv;
    float2 g_PreviousJitterUv;
    uint g_FrameIndex;
    uint g_HistoryValid;
    uint g_FilterStep;
    uint g_EffectMode;
    float g_SSGIIntensity;
    float g_SSGIMaxDistance;
    float g_SSGIHistoryWeight;
    float g_SSRMaxDistance;
    float g_SSRMaxRoughness;
    float g_SSRHistoryWeight;
    float g_Exposure;
    float g_Gamma;
    float g_BloomThreshold;
    float g_BloomIntensity;
    uint g_SSGIStepCount;
    uint g_SSRStepCount;
    uint4 g_ScreenSpacePadding;
};

#include "ModernReflection.hlsli"

Texture2D<float> g_Depth : register(t0);
Texture2D<float4> g_Normal : register(t1);
Texture2D<float4> g_Material : register(t2);
Texture2D<float4> g_HdrInput : register(t3);
Texture2D<float2> g_HiZ : register(t4);
Texture2D<float2> g_Velocity : register(t5);
Texture2D<float4> g_Current : register(t6);
Texture2D<float4> g_History : register(t7);
Texture2D<float4> g_SSGI : register(t8);
Texture2D<float4> g_SSR : register(t9);
Texture2D<float> g_PreviousDepth : register(t10);
Texture2D<float4> g_PreviousNormal : register(t11);
TextureCube<float4> g_Environment : register(t12);
Texture2D<float> g_SSAO : register(t13);
SamplerState g_LinearSampler : register(s0);
SamplerState g_PointSampler : register(s1);
RWTexture2D<float4> g_Output : register(u0);
RWTexture2D<float> g_DepthHistoryOutput : register(u1);
RWTexture2D<float4> g_NormalHistoryOutput : register(u2);

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}
float3 RGBToYCoCg(float3 c)
{
    return float3(c.r * 0.25f + c.g * 0.5f + c.b * 0.25f, c.r * 0.5f - c.b * 0.5f,
                  -c.r * 0.25f + c.g * 0.5f - c.b * 0.25f);
}

float3 YCoCgToRGB(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float2 PixelToUv(uint2 pixel)
{
    return (float2(pixel) + 0.5f) * g_FullTexelSize;
}

float2 JitterDeltaUv()
{
    return g_CurrentJitterUv - g_PreviousJitterUv;
}

float2 StableHistoryUv(float2 currentUv, float2 rasterVelocity)
{
    // Raster velocity contains the projection-jitter delta because GBuffer depth/normal are themselves jittered.
    // Accumulated radiance, however, is stored on the stable output pixel grid. Removing the jitter delta here keeps
    // a static history sample at the same output texel instead of translating the entire prior frame every sample.
    return currentUv - (rasterVelocity - JitterDeltaUv());
}

uint2 EffectPixelToFullPixel(uint2 pixel)
{
    // The effect buffer uses ceil(fullSize / 2). Deriving the representative pixel from normalized coordinates keeps
    // odd-sized render targets centered; pixel * 2 + 1 biases every sample toward the lower-right edge.
    float2 fullPosition = (float2(pixel) + 0.5f) * float2(g_FullSize) / float2(g_EffectSize);
    return min((uint2)fullPosition, g_FullSize - 1);
}

float3 DecodeWorldNormal(float3 encodedNormal)
{
    float3 normal = encodedNormal * 2.0f - 1.0f;
    return normal * rsqrt(max(dot(normal, normal), 1e-8f));
}

float3 ReconstructViewPosition(uint2 pixel, float depth)
{
    float2 uv = PixelToUv(pixel);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view = mul(float4(ndc, depth, 1.0f), g_InverseProjection);
    return view.xyz / max(abs(view.w), 1e-6f);
}

float3 ReconstructViewPositionUv(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view = mul(float4(ndc, depth, 1.0f), g_InverseProjection);
    return view.xyz / max(abs(view.w), 1e-6f);
}

float3 ReconstructWorldPositionUv(float2 uv, float depth, row_major float4x4 inverseViewProjection)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 world = mul(float4(ndc, depth, 1.0f), inverseViewProjection);
    return world.xyz / max(abs(world.w), 1e-6f);
}

bool ReprojectBackgroundUv(float2 currentUv, out float2 previousUv)
{
    previousUv = currentUv;
    // A depth-clear pixel has no GBuffer velocity. Keep its infinitely distant ray relative to the camera, move that
    // ray to the previous camera origin, and project it through the previous jittered view-projection.
    float3 farWorld = ReconstructWorldPositionUv(currentUv, 1.0f, g_InverseViewProjection);
    float3 cameraRay = farWorld - g_CameraPositionAmbient.xyz;
    if (dot(cameraRay, cameraRay) <= 1e-10f)
        return false;
    float3 previousFarWorld = g_PreviousCameraPosition.xyz + cameraRay;
    float4 previousClip = mul(float4(previousFarWorld, 1.0f), g_PreviousViewProjection);
    if (previousClip.w <= 1e-5f)
        return false;
    float2 previousNdc = previousClip.xy / previousClip.w;
    previousUv = previousNdc * float2(0.5f, -0.5f) + 0.5f;
    return all(previousUv >= 0.0f) && all(previousUv <= 1.0f);
}

bool ProjectViewPosition(float3 viewPosition, out float2 uv, out float projectedDepth)
{
    uv = 0.0f;
    projectedDepth = 0.0f;
    float4 clip = mul(float4(viewPosition, 1.0f), g_Projection);
    if (clip.w <= 1e-5f)
        return false;
    float3 ndc = clip.xyz / clip.w;
    uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    projectedDepth = ndc.z;
    return all(uv >= 0.0f) && all(uv <= 1.0f) && projectedDepth >= 0.0f && projectedDepth <= 1.0f;
}

float3 WorldNormalToView(float3 encodedNormal)
{
    float3 normalW = DecodeWorldNormal(encodedNormal);
    return normalize(mul(float4(normalW, 0.0f), g_View).xyz);
}

float ProjectViewDepth(float viewDepth)
{
    float4 clip = mul(float4(0.0f, 0.0f, viewDepth, 1.0f), g_Projection);
    float safeW = abs(clip.w) > 1e-6f ? clip.w : (clip.w < 0.0f ? -1e-6f : 1e-6f);
    return clip.z / safeW;
}

float2 LoadHiZRange(uint2 fullPixel, uint requestedMip)
{
    uint baseWidth, baseHeight, levelCount;
    g_HiZ.GetDimensions(0, baseWidth, baseHeight, levelCount);
    uint resolvedMip = min(requestedMip, max(levelCount, 1u) - 1u);

    uint mipWidth, mipHeight, ignoredLevelCount;
    g_HiZ.GetDimensions(resolvedMip, mipWidth, mipHeight, ignoredLevelCount);
    uint2 mipPixel = min(fullPixel >> resolvedMip,
                         uint2(max(mipWidth, 1u) - 1u, max(mipHeight, 1u) - 1u));
    return g_HiZ.Load(int3(mipPixel, resolvedMip));
}

bool HiZRangeOverlapsRay(float rayDepth, float rayViewDepth, float viewThickness, float2 minMaxDepth)
{
    // Standard 0..1 depth is monotonic but non-linear. Convert the accepted view-space thickness to device depth so
    // the comparison remains stable from the near plane to the far plane, and use both conservative HiZ bounds.
    float comparisonViewDepth = rayViewDepth - viewThickness;
    if (abs(g_Projection[2][3]) > 0.5f)
        comparisonViewDepth = max(comparisonViewDepth, 1e-5f);
    float nearDepth = ProjectViewDepth(comparisonViewDepth);
    float deviceThickness = abs(rayDepth - nearDepth) + 2e-4f;
    return rayDepth >= minMaxDepth.x - deviceThickness && rayDepth <= minMaxDepth.y + deviceThickness;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float RelativeLuminanceDifference(float3 a, float3 b)
{
    float luminanceA = Luminance(a);
    float luminanceB = Luminance(b);
    return abs(luminanceA - luminanceB) / max(max(luminanceA, luminanceB), 0.1f);
}

bool RefineRayDepthCrossing(float3 rayOrigin, float3 direction, float minimumDistance, float maximumDistance,
                            float viewThickness, out uint2 hitPixel, out float hitDistance)
{
    float low = minimumDistance;
    float high = maximumDistance;
    hitPixel = 0;
    hitDistance = maximumDistance;
    [unroll]
    for (uint iteration = 0; iteration < 5; ++iteration)
    {
        float middle = (low + high) * 0.5f;
        float3 rayPosition = rayOrigin + direction * middle;
        float2 uv;
        float rayDepth;
        if (!ProjectViewPosition(rayPosition, uv, rayDepth))
            return false;
        uint2 samplePixel = min((uint2)(uv * g_FullSize), g_FullSize - 1);
        float surfaceDepth = g_Depth.Load(int3(samplePixel, 0));
        if (surfaceDepth >= 0.999999f)
        {
            low = middle;
            continue;
        }
        float surfaceViewZ = ReconstructViewPosition(samplePixel, surfaceDepth).z;
        if (rayPosition.z < surfaceViewZ)
            low = middle;
        else
            high = middle;
    }

    hitDistance = high;
    float3 hitPosition = rayOrigin + direction * high;
    float2 hitUv;
    float hitDepth;
    if (!ProjectViewPosition(hitPosition, hitUv, hitDepth))
        return false;
    hitPixel = min((uint2)(hitUv * g_FullSize), g_FullSize - 1);
    float surfaceDepth = g_Depth.Load(int3(hitPixel, 0));
    if (surfaceDepth >= 0.999999f)
        return false;
    float surfaceViewZ = ReconstructViewPosition(hitPixel, surfaceDepth).z;
    float viewDelta = hitPosition.z - surfaceViewZ;
    return viewDelta >= -0.005f && viewDelta <= viewThickness * 1.5f;
}

void NeighborhoodBounds(Texture2D<float4> source, int2 pixel, uint2 size, out float3 minimum, out float3 maximum)
{
    minimum = 1e30f.xxx;
    maximum = -1e30f.xxx;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), int2(size) - 1);
        float3 value = RGBToYCoCg(source.Load(int3(samplePixel, 0)).rgb);
        minimum = min(minimum, value);
        maximum = max(maximum, value);
    }
}

float NeighborhoodMaxAlpha(Texture2D<float4> source, int2 pixel, uint2 size)
{
    float maximum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), int2(size) - 1);
        maximum = max(maximum, source.Load(int3(samplePixel, 0)).a);
    }
    return maximum;
}

void SSGINeighborhoodStatistics(Texture2D<float4> source, int2 pixel, uint2 size, out float3 minimum,
                                out float3 maximum, out float meanLuminance, out float secondMoment)
{
    minimum = 1e30f.xxx;
    maximum = -1e30f.xxx;
    meanLuminance = 0.0f;
    secondMoment = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), int2(size) - 1);
        float4 sampleValue = source.Load(int3(samplePixel, 0));
        float luminance = Luminance(sampleValue.rgb);
        float3 ycocg = RGBToYCoCg(sampleValue.rgb);
        minimum = min(minimum, ycocg);
        maximum = max(maximum, ycocg);
        meanLuminance += luminance;
        secondMoment += max(sampleValue.a, luminance * luminance);
    }
    meanLuminance /= 9.0f;
    secondMoment /= 9.0f;
}

float4 AccumulateSSGIHistory(float4 current, float4 history, int2 pixel, bool valid)
{
    float3 minimum, maximum;
    float currentMeanLuminance, currentSecondMomentMean;
    SSGINeighborhoodStatistics(g_Current, pixel, g_EffectSize, minimum, maximum, currentMeanLuminance,
                               currentSecondMomentMean);

    float historyLuminance = Luminance(history.rgb);
    float historyVariance = max(history.a - historyLuminance * historyLuminance, 0.0f);
    float currentVariance = max(currentSecondMomentMean - currentMeanLuminance * currentMeanLuminance, 0.0f);
    float weight = valid ? saturate(g_SSGIHistoryWeight) : 0.0f;
    float meanDifference = historyLuminance - currentMeanLuminance;
    float temporalVariance = lerp(currentVariance, historyVariance, weight) +
                             weight * (1.0f - weight) * meanDifference * meanDifference;
    float luminanceRadius = 3.0f * sqrt(max(temporalVariance, 0.0f)) + 0.02f;
    float3 clippingRadius = float3(luminanceRadius, luminanceRadius * 0.5f, luminanceRadius * 0.5f);
    history.rgb = YCoCgToRGB(clamp(RGBToYCoCg(history.rgb), minimum - clippingRadius, maximum + clippingRadius));

    float clampedHistoryLuminance = Luminance(history.rgb);
    history.a = clampedHistoryLuminance * clampedHistoryLuminance + historyVariance;
    float4 accumulated = lerp(current, history, weight);
    float currentLuminance = Luminance(current.rgb);
    float currentSecondMoment = max(current.a, currentLuminance * currentLuminance);
    accumulated.a = lerp(currentSecondMoment, history.a, weight);
    return accumulated;
}

float4 AccumulateSSRHistory(float4 current, float4 history, int2 pixel, bool valid)
{
    float3 minimum, maximum;
    NeighborhoodBounds(g_Current, pixel, g_EffectSize, minimum, maximum);
    history.rgb = YCoCgToRGB(clamp(RGBToYCoCg(history.rgb), minimum, maximum));
    // SSR alpha is hit confidence. Clamp it to confidence present in the current neighborhood; otherwise a
    // reprojected hit can retain high confidence after its radiance was clamped to zero, subtracting the IBL
    // fallback in the composite and leaving a dark ghost.
    history.a = min(saturate(history.a), saturate(NeighborhoodMaxAlpha(g_Current, pixel, g_EffectSize)));
    float luminanceDelta = RelativeLuminanceDifference(current.rgb, history.rgb);
    float weight = valid ? saturate(g_SSRHistoryWeight) * saturate(1.0f - luminanceDelta) : 0.0f;
    float4 accumulated = lerp(current, history, weight);
    accumulated.a = saturate(accumulated.a);
    return accumulated;
}

[numthreads(8, 8, 1)]
void CSSSGITrace(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_EffectSize)) return;
    if (g_EffectMode == 0) { g_Output[pixel] = 0.0f; return; }
    uint2 fullPixel = EffectPixelToFullPixel(pixel);
    float depth = g_Depth.Load(int3(fullPixel, 0));
    if (depth >= 0.999999f) { g_Output[pixel] = 0.0f; return; }
    float3 origin = ReconstructViewPosition(fullPixel, depth);
    float3 normal = WorldNormalToView(g_Normal.Load(int3(fullPixel, 0)).xyz);
    float angle = 6.2831853f * Hash12(float2(pixel) + g_FrameIndex * 17.0f);
    float3 tangent = normalize(abs(normal.y) < 0.99f ? cross(float3(0, 1, 0), normal)
                                                     : cross(float3(1, 0, 0), normal));
    float3 bitangent = cross(normal, tangent);
    float radial = sqrt(Hash12(float2(pixel.yx) + g_FrameIndex * 31.0f));
    float3 direction = normalize(normal * sqrt(saturate(1.0f - radial * radial)) +
                                 (tangent * cos(angle) + bitangent * sin(angle)) * radial);
    float3 indirect = 0.0f;
    float hitWeight = 0.0f;
    const float3 rayOrigin = origin + normal * 0.03f;
    float previousDistance = 0.0f;
    float previousViewDelta = -0.03f;
    [loop]
    for (uint step = 1; step <= g_SSGIStepCount; ++step)
    {
        float distance = max(g_SSGIMaxDistance, 0.1f) * ((float)step / (float)g_SSGIStepCount);
        float3 rayPosition = rayOrigin + direction * distance;
        float2 sampleUv;
        float rayDepth;
        if (!ProjectViewPosition(rayPosition, sampleUv, rayDepth)) break;
        uint2 samplePixel = min((uint2)(sampleUv * g_FullSize), g_FullSize - 1);
        float2 minMaxDepth = LoadHiZRange(samplePixel, (uint)max(log2((float)step), 0.0f));
        float viewThickness = max(0.05f, g_SSGIMaxDistance / max((float)g_SSGIStepCount, 1.0f) * 0.5f);
        const bool hiZCandidate = HiZRangeOverlapsRay(rayDepth, rayPosition.z, viewThickness, minMaxDepth);
        // A coarse min/max overlap is only a candidate. Confirm against mip 0 and retain a depth crossing even when
        // the fixed step overshoots the candidate interval; the latter is refined below instead of losing thin
        // geometry between two half-resolution samples.
        float surfaceDepth = g_Depth.Load(int3(samplePixel, 0));
        if (surfaceDepth < 0.999999f)
        {
            float surfaceViewZ = ReconstructViewPosition(samplePixel, surfaceDepth).z;
            float viewDelta = rayPosition.z - surfaceViewZ;
            // A depth overlap alone accepts the source polygon as its own hit, especially on broad floors and
            // walls. Require the ray to cross from in front of scene depth to behind it, then binary-refine the
            // interval so fixed half-resolution steps cannot jump across thin Sponza geometry.
            if (previousViewDelta < -0.002f && viewDelta >= -0.002f &&
                (hiZCandidate || viewDelta >= viewThickness))
            {
                uint2 hitPixel;
                float hitDistance;
                if (!RefineRayDepthCrossing(rayOrigin, direction, previousDistance, distance, viewThickness,
                                            hitPixel, hitDistance))
                    continue;
                float3 hitNormal = WorldNormalToView(g_Normal.Load(int3(hitPixel, 0)).xyz);
                float hitFacing = saturate(dot(hitNormal, -direction));
                if (hitFacing <= 0.05f)
                    continue;
                indirect = g_HdrInput.Load(int3(hitPixel, 0)).rgb;
                // Rays are cosine-weighted already. hitFacing only rejects back-facing geometry; multiplying it into
                // the estimate applies an extra cosine term and makes valid indirect lighting systematically dark.
                hitWeight = saturate(1.0f - hitDistance / max(g_SSGIMaxDistance, 0.1f));
                break;
            }
            previousViewDelta = viewDelta;
            previousDistance = distance;
        }
    }
    // The traced HDR value is incident radiance; the receiver BRDF is applied during full-resolution composition.
    // Keep the single-ray estimate energy-bounded before temporal accumulation.
    float3 radiance = indirect * hitWeight;
    float luminance = Luminance(radiance);
    g_Output[pixel] = float4(radiance, luminance * luminance);
}

[numthreads(8, 8, 1)]
void CSSSRTrace(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_EffectSize)) return;
    if (g_EffectMode == 0) { g_Output[pixel] = 0.0f; return; }
    uint2 fullPixel = EffectPixelToFullPixel(pixel);
    float roughness = g_Material.Load(int3(fullPixel, 0)).y;
    if (roughness > g_SSRMaxRoughness)
    {
        g_Output[pixel] = 0.0f;
        return;
    }
    float depth = g_Depth.Load(int3(fullPixel, 0));
    if (depth >= 0.999999f) { g_Output[pixel] = 0.0f; return; }
    float3 origin = ReconstructViewPosition(fullPixel, depth);
    float3 normal = WorldNormalToView(g_Normal.Load(int3(fullPixel, 0)).xyz);
    float3 direction = normalize(reflect(normalize(origin), normal));
    float roughnessFadeWidth = max(g_SSRMaxRoughness * 0.2f, 0.05f);
    float roughnessConfidence = saturate((g_SSRMaxRoughness - roughness) / roughnessFadeWidth);
    float3 reflection = 0.0f;
    float confidence = 0.0f;
    const float3 rayOrigin = origin + normal * 0.02f;
    float previousDistance = 0.0f;
    float previousViewDelta = -0.02f;
    [loop]
    for (uint step = 1; step <= g_SSRStepCount; ++step)
    {
        float distance = max(g_SSRMaxDistance, 0.1f) * ((float)step / (float)g_SSRStepCount);
        float3 rayPosition = rayOrigin + direction * distance;
        float2 sampleUv;
        float rayDepth;
        if (!ProjectViewPosition(rayPosition, sampleUv, rayDepth)) break;
        uint2 samplePixel = min((uint2)(sampleUv * g_FullSize), g_FullSize - 1);
        float2 minMaxDepth = LoadHiZRange(samplePixel, (uint)max(log2((float)step), 0.0f));
        float viewThickness = max(0.03f, g_SSRMaxDistance / max((float)g_SSRStepCount, 1.0f) * 0.35f +
                                                   roughness * 0.05f);
        const bool hiZCandidate = HiZRangeOverlapsRay(rayDepth, rayPosition.z, viewThickness, minMaxDepth);
        float surfaceDepth = g_Depth.Load(int3(samplePixel, 0));
        if (surfaceDepth < 0.999999f)
        {
            float surfaceViewZ = ReconstructViewPosition(samplePixel, surfaceDepth).z;
            float viewDelta = rayPosition.z - surfaceViewZ;
            if (previousViewDelta < -0.002f && viewDelta >= -0.002f &&
                (hiZCandidate || viewDelta >= viewThickness))
            {
                uint2 hitPixel;
                float hitDistance;
                if (!RefineRayDepthCrossing(rayOrigin, direction, previousDistance, distance, viewThickness,
                                            hitPixel, hitDistance))
                    continue;
                float3 hitNormal = WorldNormalToView(g_Normal.Load(int3(hitPixel, 0)).xyz);
                float hitFacing = saturate(dot(hitNormal, -direction));
                if (hitFacing <= 0.03f)
                    continue;
                reflection = g_HdrInput.Load(int3(hitPixel, 0)).rgb;
                float2 edge = abs((float2(hitPixel) + 0.5f) / g_FullSize * 2.0f - 1.0f);
                confidence = saturate(1.0f - max(edge.x, edge.y)) * hitFacing *
                             roughnessConfidence * saturate(1.0f - hitDistance / max(g_SSRMaxDistance, 0.1f));
                break;
            }
            previousViewDelta = viewDelta;
            previousDistance = distance;
        }
    }
    g_Output[pixel] = float4(reflection, confidence);
}

[numthreads(8, 8, 1)]
void CSTemporal(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_EffectSize)) return;
    float4 current = g_Current.Load(int3(pixel, 0));
    uint2 fullPixel = EffectPixelToFullPixel(pixel);
    float2 velocity = g_Velocity.Load(int3(fullPixel, 0));
    // The history texture is half resolution, so its current sample lives at the effect texel center. Depth and
    // normal histories are full resolution and must instead follow the representative GBuffer pixel whose velocity
    // we loaded. Keeping both coordinate spaces prevents a systematic half-texel history shift.
    const float2 currentEffectUv = (float2(pixel) + 0.5f) * g_EffectTexelSize;
    const float2 currentGeometryUv = PixelToUv(fullPixel);
    float2 historyUv = StableHistoryUv(currentEffectUv, velocity);
    float2 previousGeometryUv = currentGeometryUv - velocity;
    bool valid = g_HistoryValid != 0 && all(historyUv >= 0.0f) && all(historyUv <= 1.0f) &&
                 all(previousGeometryUv >= 0.0f) && all(previousGeometryUv <= 1.0f);
    float4 history = valid ? g_History.SampleLevel(g_LinearSampler, historyUv, 0.0f) : current;
    const float currentDepth = g_Depth.Load(int3(fullPixel, 0));
    const float previousDepth = g_PreviousDepth.SampleLevel(g_PointSampler, previousGeometryUv, 0.0f);
    const float3 currentWorld =
        ReconstructWorldPositionUv(currentGeometryUv, currentDepth, g_InverseViewProjection);
    const float3 previousWorld =
        ReconstructWorldPositionUv(previousGeometryUv, previousDepth, g_PreviousInverseViewProjection);
    const float currentViewDepth = mul(float4(currentWorld, 1.0f), g_View).z;
    const float worldThreshold = max(0.05f, abs(currentViewDepth) * 0.01f);
    valid = valid && currentDepth < 0.999999f && previousDepth < 0.999999f &&
            length(currentWorld - previousWorld) < worldThreshold;
    float3 currentNormal = DecodeWorldNormal(g_Normal.Load(int3(fullPixel, 0)).xyz);
    float3 historyNormal =
        DecodeWorldNormal(g_PreviousNormal.SampleLevel(g_PointSampler, previousGeometryUv, 0.0f).xyz);
    valid = valid && dot(currentNormal, historyNormal) > 0.9f;
    g_Output[pixel] = g_EffectMode == 1u ? AccumulateSSGIHistory(current, history, int2(pixel), valid)
                                         : AccumulateSSRHistory(current, history, int2(pixel), valid);
}

[numthreads(8, 8, 1)]
void CSAtrous(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_EffectSize)) return;
    uint2 fullPixel = EffectPixelToFullPixel(pixel);
    float centerDepth = g_Depth.Load(int3(fullPixel, 0));
    float centerViewDepth = ReconstructViewPosition(fullPixel, centerDepth).z;
    float3 centerNormal = DecodeWorldNormal(g_Normal.Load(int3(fullPixel, 0)).xyz);
    float centerRoughness = g_Material.Load(int3(fullPixel, 0)).y;
    float4 centerValue = g_Current.Load(int3(pixel, 0));
    float centerLuminance = Luminance(centerValue.rgb);
    float centerVariance = max(centerValue.a - centerLuminance * centerLuminance, 0.0f);
    float4 sum = 0.0f;
    float3 confidenceWeightedColor = 0.0f;
    float confidenceSum = 0.0f;
    float weightSum = 0.0f;
    int stepWidth = 1 << g_FilterStep;
    bool ssrFilter = (g_EffectMode & 2u) != 0;
    [unroll]
    for (int tap = -2; tap <= 2; ++tap)
    {
        int2 direction = (g_EffectMode & 1u) == 0 ? int2(1, 0) : int2(0, 1);
        int2 q = clamp(int2(pixel) + direction * tap * stepWidth, int2(0, 0), int2(g_EffectSize) - 1);
        uint2 qFull = EffectPixelToFullPixel(uint2(q));
        float qDepth = g_Depth.Load(int3(qFull, 0));
        float qViewDepth = ReconstructViewPosition(qFull, qDepth).z;
        float3 qNormal = DecodeWorldNormal(g_Normal.Load(int3(qFull, 0)).xyz);
        float spatial = exp(-0.5f * tap * tap);
        float bilateral = exp(-abs(qViewDepth - centerViewDepth) / max(abs(centerViewDepth) * 0.02f, 0.05f)) *
                          pow(saturate(dot(centerNormal, qNormal)), 16.0f);
        float4 sampleValue = g_Current.Load(int3(q, 0));
        if (ssrFilter)
        {
            float qRoughness = g_Material.Load(int3(qFull, 0)).y;
            bilateral *= exp(-abs(qRoughness - centerRoughness) * 12.0f);
        }
        else
        {
            float qLuminance = Luminance(g_Current.Load(int3(q, 0)).rgb);
            bilateral *=
                exp(-abs(qLuminance - centerLuminance) / max(sqrt(centerVariance) * 4.0f, 0.04f));
        }
        float weight = spatial * bilateral;
        if (ssrFilter)
        {
            float confidence = saturate(sampleValue.a);
            confidenceWeightedColor += sampleValue.rgb * (weight * confidence);
            confidenceSum += weight * confidence;
        }
        else
        {
            sum += sampleValue * weight;
        }
        weightSum += weight;
    }
    if (ssrFilter)
    {
        float filteredConfidence = confidenceSum / max(weightSum, 1e-5f);
        float3 filteredColor = confidenceSum > 1e-5f ? confidenceWeightedColor / confidenceSum : 0.0f;
        g_Output[pixel] = float4(filteredColor, filteredConfidence);
    }
    else
    {
        g_Output[pixel] = sum / max(weightSum, 1e-5f);
    }
}

float4 BilateralUpsample(Texture2D<float4> source, uint2 pixel)
{
    float depth = g_Depth.Load(int3(pixel, 0));
    if (depth >= 0.999999f)
        return 0.0f;
    float3 centerPosition = ReconstructViewPosition(pixel, depth);
    float3 centerNormal = DecodeWorldNormal(g_Normal.Load(int3(pixel, 0)).xyz);
    float2 effectPosition = (float2(pixel) + 0.5f) * float2(g_EffectSize) / float2(g_FullSize) - 0.5f;
    int2 basePixel = int2(floor(effectPosition));
    float2 fraction = frac(effectPosition);
    float4 sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    [unroll]
    for (int x = 0; x < 2; ++x)
    {
        int2 effectPixel = clamp(basePixel + int2(x, y), int2(0, 0), int2(g_EffectSize) - 1);
        uint2 representative = EffectPixelToFullPixel(uint2(effectPixel));
        float sampleDepth = g_Depth.Load(int3(representative, 0));
        if (sampleDepth >= 0.999999f)
            continue;
        float sampleViewZ = ReconstructViewPosition(representative, sampleDepth).z;
        float3 sampleNormal = DecodeWorldNormal(g_Normal.Load(int3(representative, 0)).xyz);
        float spatial = (x == 0 ? 1.0f - fraction.x : fraction.x) *
                        (y == 0 ? 1.0f - fraction.y : fraction.y);
        float depthWeight = exp(-abs(sampleViewZ - centerPosition.z) /
                                max(abs(centerPosition.z) * 0.015f, 0.025f));
        float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), 24.0f);
        float weight = spatial * depthWeight * normalWeight;
        sum += source.Load(int3(effectPixel, 0)) * weight;
        weightSum += weight;
    }
    if (weightSum <= 1e-5f)
    {
        int2 nearest = clamp(int2(round(effectPosition)), int2(0, 0), int2(g_EffectSize) - 1);
        return source.Load(int3(nearest, 0));
    }
    return sum / weightSum;
}

float3 FresnelSchlickScreen(float cosineTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosineTheta), 5.0f);
}

float2 EnvBrdfApproxScreen(float roughness, float nDotV)
{
    float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * nDotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

[numthreads(8, 8, 1)]
void CSEffectsComposite(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_FullSize)) return;
    // Debug views are explicit full-resolution visualizations. The working SSGI/SSR buffers are half-resolution
    // linear-HDR data, and SSR stores confidence in alpha, so exposing those SRVs directly produces a misleading
    // black/grey viewport and never shows the requested confidence channel.
    if ((g_EffectMode & 4u) != 0)
    {
        float3 radiance = max(BilateralUpsample(g_SSGI, pixel).rgb, 0.0f);
        g_Output[pixel] = float4(1.0f - exp(-radiance * 8.0f), 1.0f);
        return;
    }
    if ((g_EffectMode & 8u) != 0)
    {
        float confidence = saturate(BilateralUpsample(g_SSR, pixel).a);
        g_Output[pixel] = float4(confidence.xxx, 1.0f);
        return;
    }
    float3 hdr = g_HdrInput.Load(int3(pixel, 0)).rgb;
    float3 albedo = g_Current.Load(int3(pixel, 0)).rgb;
    float3 normal = DecodeWorldNormal(g_Normal.Load(int3(pixel, 0)).xyz);
    float4 reflection = (g_EffectMode & 2u) != 0 ? BilateralUpsample(g_SSR, pixel) : 0.0f;
    float4 material = g_Material.Load(int3(pixel, 0));
    float roughness = saturate(material.y);
    float metallic = saturate(material.x);
    float ao = saturate(material.z);
    float3 gi = 0.0f;
    float3 specularCorrection = 0.0f;
    float depth = g_Depth.Load(int3(pixel, 0));
    if (depth < 0.999999f)
    {
        float3 worldPosition =
            ReconstructWorldPositionUv(PixelToUv(pixel), depth, g_InverseViewProjection);
        float3 viewDirection = normalize(g_CameraPositionAmbient.xyz - worldPosition);
        float nDotV = saturate(dot(normal, viewDirection));
        float3 f0 = lerp(0.04f.xxx, albedo, metallic);
        if ((g_EffectMode & 1u) != 0)
        {
            // SSGI stores incoming radiance. Apply the receiver's diffuse response here so metallic surfaces do not
            // gain an unphysical diffuse lobe and material AO remains consistent with clustered environment lighting.
            float3 fresnel = FresnelSchlickScreen(nDotV, f0);
            float3 diffuseResponse = (1.0f - fresnel) * (1.0f - metallic) * albedo * ao;
            // Keep temporal history in unscaled radiance space. The art-directed intensity belongs at the final
            // material composition point so changing it is immediate and does not invalidate accumulated history.
            gi = BilateralUpsample(g_SSGI, pixel).rgb * diffuseResponse * max(g_SSGIIntensity, 0.0f);
        }
        if ((g_EffectMode & 2u) != 0 && reflection.a > 0.0f)
        {
            float2 brdf = EnvBrdfApproxScreen(roughness, nDotV);
            float3 brdfFactor =
                max(f0 * brdf.x + brdf.y, 0.0f) * ao * max(g_CameraPositionAmbient.w, 0.0f);
            float3 reflectionDirection =
                ModernWorldReflectionDirection(worldPosition, g_CameraPositionAmbient.xyz, normal);
            float3 environmentRadiance =
                g_Environment.SampleLevel(g_LinearSampler, reflectionDirection, roughness * 6.0f).rgb;
            // Clustered lighting already contains this environment term. Replace it with the confident screen hit
            // instead of adding a second specular lobe; confidence naturally preserves the environment fallback.
            specularCorrection = (reflection.rgb - environmentRadiance) * brdfFactor * saturate(reflection.a);
        }
    }
    const float screenSpaceAO =
        (g_EffectMode & 16u) != 0 ? saturate(g_SSAO.SampleLevel(g_LinearSampler, PixelToUv(pixel), 0.0f)) : 1.0f;
    g_Output[pixel] = float4(max((hdr + gi + specularCorrection) * screenSpaceAO, 0.0f), 1.0f);
}
float3 AcesToneMap(float3 color)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

[numthreads(8, 8, 1)]
void CSBloomTone(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_FullSize)) return;
    float3 color = g_Current.Load(int3(pixel, 0)).rgb;
    if (g_BloomIntensity > 0.0001f)
    {
        float3 bloom = 0.0f;
        [unroll]
        for (int y = -2; y <= 2; ++y)
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            int2 q = clamp(int2(pixel) + int2(x, y) * 2, int2(0, 0), int2(g_FullSize) - 1);
            float3 sampleColor = g_Current.Load(int3(q, 0)).rgb;
            bloom += max(sampleColor - g_BloomThreshold, 0.0f);
        }
        color += bloom * (g_BloomIntensity / 25.0f);
    }
    color = AcesToneMap(color * max(g_Exposure, 0.0f));
    color = pow(saturate(color), 1.0f / max(g_Gamma, 0.1f));
    g_Output[pixel] = float4(color, 1.0f);
    g_DepthHistoryOutput[pixel] = g_Depth.Load(int3(pixel, 0));
    g_NormalHistoryOutput[pixel] = g_Normal.Load(int3(pixel, 0));
}
