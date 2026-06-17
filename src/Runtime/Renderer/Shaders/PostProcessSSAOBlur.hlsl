// PostProcessSSAOBlur.hlsl - 4-tap bilateral blur for SSAO
// g_TexelSize = (1/w, isVertical, 1/h, 0)

Texture2D    g_SSAOInput   : register(t0);
SamplerState g_SSAOSampler : register(s0);

cbuffer BlurParams : register(b0)
{
    float4 g_TexelSize;     // (1/w, isVertical, 1/h, 0)
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((vertexId << 1) & 2, vertexId & 2);
    o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    bool   vert  = g_TexelSize.y > 0.5f;
    float  ts    = vert ? g_TexelSize.z : g_TexelSize.x;
    float2 dir   = vert ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f);

    float center = g_SSAOInput.SampleLevel(g_SSAOSampler, input.uv, 0).r;

    // Gaussian-like kernel: centre weight + 4 bilateral pairs
    float  result = center * 0.4f;
    float  wSum   = 0.4f;

    const float wt[4] = { 0.20f, 0.12f, 0.06f, 0.03f };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float off   = float(i + 1) * ts;
        float s0    = g_SSAOInput.SampleLevel(g_SSAOSampler, input.uv + dir * off, 0).r;
        float s1    = g_SSAOInput.SampleLevel(g_SSAOSampler, input.uv - dir * off, 0).r;

        // bilateral weight: penalise large AO differences (depth edges)
        float w0    = wt[i] / (1.0f + abs(s0 - center) * 4.0f);
        float w1    = wt[i] / (1.0f + abs(s1 - center) * 4.0f);

        result += s0 * w0 + s1 * w1;
        wSum   += w0 + w1;
    }

    result /= wSum;

    return float4(result, result, result, 1.0f);
}
