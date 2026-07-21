#ifndef MYENGINE_ENVIRONMENT_RADIANCE_HLSLI
#define MYENGINE_ENVIRONMENT_RADIANCE_HLSLI

static const float ENV_PI = 3.14159265359f;

// Analytical atmosphere sky model.
// direction: world-space view direction (Y-up, +Z forward, left-handed).
// Returns HDR radiance in the given direction.
float3 EnvironmentRadiance(
    float3 direction,
    float3 sunDirection,
    float3 skyTint,
    float3 horizonTint,
    float3 groundTint)
{
    sunDirection = normalize(sunDirection);
    float mu = dot(direction, sunDirection);
    float skyMask = smoothstep(-0.025f, 0.025f, direction.y);
    float horizonMask = 1.0f - smoothstep(0.015f, 0.18f, abs(direction.y));
    float skyHeight = saturate(max(direction.y, 0.0f));
    float horizonAirMass = 1.0f / max(skyHeight + 0.08f, 0.08f);
    horizonAirMass = min(horizonAirMass, 12.0f);

    float3 betaRayleigh = float3(5.8e-3f, 13.5e-3f, 33.1e-3f);
    float3 betaMie = float3(21.0e-3f, 21.0e-3f, 21.0e-3f);
    float rayleighPhase = 3.0f / (16.0f * ENV_PI) * (1.0f + mu * mu);

    float g = 0.76f;
    float mieDenom = max(1.0f + g * g - 2.0f * g * mu, 1e-3f);
    float miePhase = 3.0f / (8.0f * ENV_PI) *
        ((1.0f - g * g) * (1.0f + mu * mu)) /
        ((2.0f + g * g) * pow(mieDenom, 1.5f));

    float sunHeight = saturate(sunDirection.y * 0.5f + 0.5f);
    float3 sunTransmittance = exp(-(betaRayleigh + betaMie * 0.25f) *
        (1.0f / max(sunDirection.y + 0.12f, 0.12f)));
    float3 inscatter = (betaRayleigh * rayleighPhase + betaMie * miePhase) *
        sunTransmittance * horizonAirMass * 35.0f;

    float3 extinction = exp(-(betaRayleigh + betaMie) * horizonAirMass);
    float3 groundRadiance = float3(0.010f, 0.012f, 0.014f) * groundTint *
        (0.35f + 0.65f * sunHeight);
    float3 horizonHaze = float3(0.055f, 0.075f, 0.105f) * horizonTint * horizonMask *
        (0.35f + 0.65f * sunHeight);

    float sunDisc = smoothstep(0.99980f, 0.99996f, mu);
    float sunGlow = pow(saturate(mu), 2048.0f);
    float3 solarRadiance = float3(24.0f, 20.0f, 14.0f) * sunTransmittance;

    float3 skyRadiance = (inscatter * (1.0f - extinction * 0.45f) +
        solarRadiance * (sunDisc + sunGlow * 0.35f)) * skyTint;
    return lerp(groundRadiance + horizonHaze, skyRadiance + horizonHaze, skyMask);
}

// ACES filmic tone mapping (Narkowicz fit).
float3 AcesToneMap(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

#endif
