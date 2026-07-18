#ifndef MYENGINE_MODERN_REFLECTION_HLSLI
#define MYENGINE_MODERN_REFLECTION_HLSLI

float3 ModernWorldReflectionDirection(float3 worldPosition, float3 cameraPosition, float3 worldNormal)
{
    float3 incidentDirection = normalize(worldPosition - cameraPosition);
    float3 normal = normalize(worldNormal);
    return normalize(reflect(incidentDirection, normal));
}

#endif
