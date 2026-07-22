#include "ModernRayTracing.hlsli"

#if defined(MYENGINE_RT_SHADOW)
RWTexture2DArray<float> g_RTShadowOutput : register(u0);
#elif defined(MYENGINE_RT_AO) || defined(MYENGINE_RT_DIFFUSE) || defined(MYENGINE_RT_REFLECTION)
RWTexture2D<float4> g_RTOutput : register(u0);
#else
#error "ModernRayTracing.hlsl requires exactly one MYENGINE_RT_* variant define"
#endif

#if defined(MYENGINE_RT_SHADOW)
[numthreads(8, 8, 1)]
void CSRTShadow(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RTFullSize))
        return;
    float depth = g_RTDepth.Load(int3(pixel, 0));
    if (depth >= 0.999999f)
    {
        g_RTShadowOutput[uint3(pixel, 0)] = 1.0f;
        return;
    }
    float3 normal = ModernRTDecodeNormal(g_RTNormal.Load(int3(pixel, 0)).xyz);
    float3 worldPosition = ModernRTReconstructWorldPosition(pixel, depth);
    float3 direction = normalize(-g_RTLightDirectionIntensity.xyz);
    float visible = ModernRTVisible(worldPosition + normal * max(g_RTParams0.y, 0.001f), direction, 10000.0f) ? 1.0f : 0.0f;
    g_RTShadowOutput[uint3(pixel, 0)] = lerp(1.0f, visible, saturate(g_RTParams1.z));
}

#elif defined(MYENGINE_RT_AO)
[numthreads(8, 8, 1)]
void CSRTAO(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RTEffectSize))
        return;
    uint2 fullPixel = ModernRTEffectPixelToFullPixel(pixel);
    float depth = g_RTDepth.Load(int3(fullPixel, 0));
    if (depth >= 0.999999f)
    {
        g_RTOutput[pixel] = 1.0f.xxxx;
        return;
    }
    float3 normal = ModernRTDecodeNormal(g_RTNormal.Load(int3(fullPixel, 0)).xyz);
    float3 origin = ModernRTReconstructWorldPosition(fullPixel, depth) + normal * max(g_RTParams0.y, 0.001f);
    const float2 randomValue = ModernRTSample2D(pixel, (uint)g_RTParams1.w, 0u);
    float3 direction = ModernRTCosineDirection(normal, randomValue);
    uint instanceId;
    uint primitiveIndex;
    float2 barycentrics;
    float hitDistance;
    const float radius = max(g_RTParams0.x, 0.01f);
    const bool occluded = ModernRTTrace(origin, direction, 0.001f, radius, instanceId, primitiveIndex, barycentrics,
                                        hitDistance);
    float ao = occluded ? saturate(hitDistance / radius) : 1.0f;
    ao = pow(ao, max(g_RTParams0.z, 0.1f));
    ao = lerp(1.0f, ao, saturate(g_RTParams0.w));
    g_RTOutput[pixel] = float4(ao, ao, ao, 1.0f);
}

#elif defined(MYENGINE_RT_DIFFUSE)
[numthreads(8, 8, 1)]
void CSRTDiffuse(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RTEffectSize))
        return;
    uint2 fullPixel = ModernRTEffectPixelToFullPixel(pixel);
    float depth = g_RTDepth.Load(int3(fullPixel, 0));
    if (depth >= 0.999999f)
    {
        g_RTOutput[pixel] = 0.0f.xxxx;
        return;
    }
    float3 normal = ModernRTDecodeNormal(g_RTNormal.Load(int3(fullPixel, 0)).xyz);
    float3 origin = ModernRTReconstructWorldPosition(fullPixel, depth) + normal * 0.002f;
    const float2 randomValue = ModernRTSample2D(pixel, (uint)g_RTParams1.w, 1u);
    float3 direction = ModernRTCosineDirection(normal, randomValue);
    uint instanceId;
    uint primitiveIndex;
    float2 barycentrics;
    float hitDistance;
    float3 radiance = ModernRTTrace(origin, direction, 0.001f, max(g_RTParams1.x, 0.1f), instanceId,
                                    primitiveIndex, barycentrics, hitDistance)
                          ? ModernRTSurfaceGiRadiance(instanceId, primitiveIndex, barycentrics, -direction)
                          : 0.0f;
    // Keep temporal history in unscaled incoming-radiance space; the shared effects composite applies SSGI intensity.
    // Alpha follows the SSGI trace ABI and stores the luminance second moment used by temporal variance clipping.
    const float3 nonNegativeRadiance = max(radiance, 0.0f);
    const float luminance = dot(nonNegativeRadiance, float3(0.2126f, 0.7152f, 0.0722f));
    g_RTOutput[pixel] = float4(nonNegativeRadiance, luminance * luminance);
}

#elif defined(MYENGINE_RT_REFLECTION)
[numthreads(8, 8, 1)]
void CSRTReflection(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= g_RTEffectSize))
        return;
    uint2 fullPixel = ModernRTEffectPixelToFullPixel(pixel);
    float depth = g_RTDepth.Load(int3(fullPixel, 0));
    if (depth >= 0.999999f)
    {
        g_RTOutput[pixel] = 0.0f.xxxx;
        return;
    }
    float3 normal = ModernRTDecodeNormal(g_RTNormal.Load(int3(fullPixel, 0)).xyz);
    float3 worldPosition = ModernRTReconstructWorldPosition(fullPixel, depth);
    float roughness = saturate(g_RTMaterialBuffer.Load(int3(fullPixel, 0)).y);
    if (roughness > g_RTParams1.y)
    {
        g_RTOutput[pixel] = 0.0f.xxxx;
        return;
    }
    float3 viewDirection = normalize(g_RTCameraPositionAmbient.xyz - worldPosition);
    const float2 randomValue = ModernRTSample2D(pixel, (uint)g_RTParams1.w, 2u);
    float3 direction = ModernRTGGXReflectionDirection(normal, viewDirection, roughness, randomValue);
    uint instanceId;
    uint primitiveIndex;
    float2 barycentrics;
    float hitDistance;
    float3 radiance = ModernRTTrace(worldPosition + normal * 0.002f, direction, 0.001f,
                                    max(g_RTParams1.x, 0.1f), instanceId, primitiveIndex, barycentrics, hitDistance)
                          ? ModernRTSurfaceRadiance(instanceId, primitiveIndex, barycentrics)
                          : g_RTEnvironment.SampleLevel(g_RTLinearSampler, direction, roughness * 6.0f).rgb;
    g_RTOutput[pixel] = float4(max(radiance, 0.0f), 1.0f - roughness);
}
#endif
