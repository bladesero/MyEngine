cbuffer TemporalAAConstants : register(b0)
{
    row_major float4x4 g_InverseJitteredViewProjection;
    row_major float4x4 g_PreviousUnjitteredViewProjection;
    uint2 g_RenderSize;
    float2 g_TexelSize;
    float2 g_CurrentJitterUv;
    uint g_HistoryValid;
    uint g_DebugMode;
    float g_HistoryWeight;
    float g_HistoryClipExpansion;
    float2 g_TAAPadding;
};

Texture2D<float4> g_Current : register(t0);
Texture2D<float4> g_History : register(t1);
Texture2D<float> g_Depth : register(t2);
SamplerState g_LinearSampler : register(s0);
RWTexture2D<float4> g_Output : register(u0);
RWTexture2D<float4> g_TAADebugOutput : register(u1);

float3 RGBToYCoCg(float3 color)
{
    return float3(color.r * 0.25f + color.g * 0.5f + color.b * 0.25f,
                  color.r * 0.5f - color.b * 0.5f,
                  -color.r * 0.25f + color.g * 0.5f - color.b * 0.25f);
}

float3 YCoCgToRGB(float3 color)
{
    const float temporary = color.x - color.z;
    return float3(temporary + color.y, color.x + color.z, temporary - color.y);
}

float2 PixelToUv(uint2 pixel)
{
    return (float2(pixel) + 0.5f) * g_TexelSize;
}

bool ReprojectToPreviousFrame(float2 currentUv, float depth, out float2 previousUv)
{
    const float2 currentNdc = float2(currentUv.x * 2.0f - 1.0f, 1.0f - currentUv.y * 2.0f);
    const float4 worldHomogeneous =
        mul(float4(currentNdc, depth, 1.0f), g_InverseJitteredViewProjection);
    if (abs(worldHomogeneous.w) <= 1e-6f)
    {
        previousUv = currentUv;
        return false;
    }

    const float3 worldPosition = worldHomogeneous.xyz / worldHomogeneous.w;
    const float4 previousClip =
        mul(float4(worldPosition, 1.0f), g_PreviousUnjitteredViewProjection);
    if (previousClip.w <= 1e-5f)
    {
        previousUv = currentUv;
        return false;
    }

    const float2 previousNdc = previousClip.xy / previousClip.w;
    previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);
    return all(previousUv >= 0.0f) && all(previousUv <= 1.0f);
}

void CurrentNeighborhoodMoments(float2 unjitteredUv, out float3 mean, out float3 variance)
{
    mean = 0.0f.xxx;
    float3 secondMoment = 0.0f.xxx;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = unjitteredUv + float2(x, y) * g_TexelSize;
            const float3 sampleYCoCg =
                RGBToYCoCg(g_Current.SampleLevel(g_LinearSampler, sampleUv, 0.0f).rgb);
            mean += sampleYCoCg;
            secondMoment += sampleYCoCg * sampleYCoCg;
        }
    }
    mean /= 9.0f;
    secondMoment /= 9.0f;
    variance = max(secondMoment - mean * mean, 0.0f.xxx);
}

float3 TAAHistoryAgeDebugColor(float historyAge)
{
    const float age120 = saturate(historyAge / 120.0f);
    return float3(saturate(2.0f - 2.0f * age120), saturate(2.0f * age120), 0.0f);
}

float3 TAARejectReasonDebugColor(uint reason)
{
    if (reason == 1u)
        return 0.25f.xxx;
    if (reason == 2u)
        return float3(1.0f, 0.35f, 0.0f);
    if (reason == 3u)
        return float3(1.0f, 0.0f, 0.0f);
    return float3(0.0f, 1.0f, 0.0f);
}

[numthreads(8, 8, 1)]
void CSTAA(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RenderSize))
        return;

    const float2 currentUv = PixelToUv(pixel);
    // The main pass is rasterized with the jittered projection. Sampling at the shifted UV recovers the stable
    // current-frame signal before temporal accumulation, matching the WebGPU reference implementation.
    const float2 unjitteredUv = currentUv + g_CurrentJitterUv;
    const float2 minimumUv = g_TexelSize * 0.5f;
    const float2 maximumUv = 1.0f.xx - minimumUv;
    const float2 clampedUnjitteredUv = clamp(unjitteredUv, minimumUv, maximumUv);
    const float4 current = g_Current.SampleLevel(g_LinearSampler, clampedUnjitteredUv, 0.0f);

    float2 previousUv;
    const uint2 currentSamplePixel =
        min(uint2(clampedUnjitteredUv * float2(g_RenderSize)), g_RenderSize - 1u);
    const float currentDepth = g_Depth.Load(int3(currentSamplePixel, 0));
    const bool reprojectionValid =
        ReprojectToPreviousFrame(clampedUnjitteredUv, currentDepth, previousUv);
    const bool historyValid = g_HistoryValid != 0u && reprojectionValid;
    float4 history = historyValid ? g_History.SampleLevel(g_LinearSampler, previousUv, 0.0f) : current;

    float3 neighborhoodMean;
    float3 neighborhoodVariance;
    CurrentNeighborhoodMoments(clampedUnjitteredUv, neighborhoodMean, neighborhoodVariance);
    const float3 standardDeviation = sqrt(neighborhoodVariance);
    const float clipSigma = 1.5f * (1.0f + max(g_HistoryClipExpansion, 0.0f));
    float3 historyYCoCg = RGBToYCoCg(history.rgb);
    historyYCoCg = clamp(historyYCoCg, neighborhoodMean - clipSigma * standardDeviation,
                         neighborhoodMean + clipSigma * standardDeviation);
    history.rgb = YCoCgToRGB(historyYCoCg);

    const float historyWeight = historyValid ? saturate(g_HistoryWeight) : 0.0f;
    const float3 resolved = lerp(current.rgb, history.rgb, historyWeight);
    const float nextHistoryAge = historyValid ? min(max(history.a, 0.0f) + 1.0f, 127.0f) : 1.0f;
    g_Output[pixel] = float4(resolved, nextHistoryAge);

    uint rejectReason = 0u;
    if (g_HistoryValid == 0u)
        rejectReason = 1u;
    else if (!reprojectionValid)
        rejectReason = 2u;
    if (g_DebugMode == 16u)
        g_TAADebugOutput[pixel] = float4(TAAHistoryAgeDebugColor(nextHistoryAge), 1.0f);
    else if (g_DebugMode == 32u)
        g_TAADebugOutput[pixel] = float4(TAARejectReasonDebugColor(rejectReason), 1.0f);
}
