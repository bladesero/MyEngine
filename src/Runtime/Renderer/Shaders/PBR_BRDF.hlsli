#ifndef MYENGINE_PBR_BRDF_HLSLI
#define MYENGINE_PBR_BRDF_HLSLI

static const float BRDF_PI = 3.14159265359f;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(BRDF_PI * denom * denom, 1e-5f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-5f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(N, V)), roughness) *
           GeometrySchlickGGX(saturate(dot(N, L)), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}


float3 PbrDirectLighting(float3 albedo, float metallic, float roughness,
                         float3 N, float3 V, float3 L, float3 radiance)
{
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 specular = (D * G * F) /
        max(4.0f * saturate(dot(N, V)) * NdotL, 1e-4f);
    float3 kD = (1.0f - F) * (1.0f - metallic);
    return (kD * albedo / BRDF_PI + specular) * NdotL * radiance;
}

// Lazarov analytical BRDF LUT for split-sum IBL specular.
// Returns (scale, bias) so that specularIBL = prefilteredColor * (F0 * scale + bias).
// Reference: "Physically Based Shading in Theory and Practice", Lazarov 2013.
float2 EnvBrdfApprox(float roughness, float NdotV)
{
    float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// Split-sum PBR environment lighting.
// irradiance : cosine-convolved + PI-divided (from SH2 or analytical sky / PI).
// prefilteredColor : roughness-mipped environment cubemap sample at reflection direction.
float3 PbrEnvironmentLighting(float3 albedo, float metallic, float roughness,
                              float ao, float3 N, float3 V,
                              float3 irradiance, float3 prefilteredColor)
{
    float NdotV = saturate(dot(N, V));
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // Fresnel split for energy conservation (standard Schlick, not roughness-aware).
    float3 F = FresnelSchlick(NdotV, F0);
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

    // Diffuse IBL: kD * albedo * irradiance.
    // Specular IBL: prefilteredColor * (F0 * scale + bias) via Lazarov analytical BRDF LUT.
    float3 diffuse = kD * albedo * irradiance;
    float2 brdf = EnvBrdfApprox(roughness, NdotV);
    float3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    return (diffuse + specular) * ao;
}

#endif
