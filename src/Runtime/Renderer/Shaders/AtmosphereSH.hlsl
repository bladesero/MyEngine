TextureCube<float4> g_EnvironmentCube : register(t0);
SamplerState g_EnvironmentSampler : register(s0);
RWStructuredBuffer<float4> g_SH2Out : register(u0);

static const float PI = 3.14159265359f;

float3 DirectionFromSample(uint index, uint sampleCount)
{
    const float goldenAngle = 2.39996323f;
    float z = 1.0f - 2.0f * ((float)index + 0.5f) / (float)sampleCount;
    float radius = sqrt(max(0.0f, 1.0f - z * z));
    float phi = goldenAngle * (float)index;
    return float3(cos(phi) * radius, z, sin(phi) * radius);
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

[numthreads(9, 1, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
    uint coeff = threadId.x;
    if (coeff >= 9) return;

    const uint sampleCount = 1024;
    float3 sum = 0.0f;
    for (uint i = 0; i < sampleCount; ++i) {
        float3 direction = DirectionFromSample(i, sampleCount);
        float basis[9];
        EvalSH2(direction, basis);
        float3 radiance = g_EnvironmentCube.SampleLevel(
            g_EnvironmentSampler, direction, 0.0f).rgb;
        sum += radiance * basis[coeff];
    }

    float3 shCoeff = sum * (4.0f * PI / (float)sampleCount);

    // Cosine lobe convolution for irradiance, pre-divided by PI.
    // band 0 (l=0): weight = 1.0
    // band 1 (l=1): weight = 2/3
    // band 2 (l=2): weight = 1/4
    float weight = 1.0f;
    if (coeff >= 1 && coeff <= 3) {
        weight = 2.0f / 3.0f;
    } else if (coeff >= 4) {
        weight = 1.0f / 4.0f;
    }

    g_SH2Out[coeff] = float4(shCoeff * weight, 0.0f);
}
