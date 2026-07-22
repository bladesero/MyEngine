// PostProcessSSAO.hlsl - Screen-space ambient occlusion
// Engine: LH (Y-up), row-vector, depth [0,1]. View space: +Z forward.

Texture2D    g_DepthTex      : register(t0);
SamplerState g_DepthSampler  : register(s0);
Texture2D    g_NoiseTex      : register(t1);
SamplerState g_NoiseSampler  : register(s1);

cbuffer SSAOParams : register(b0)
{
    row_major float4x4 g_Projection;
    row_major float4x4 g_InvProjection;
    float4   g_ScreenSize;      // (1/w, 1/h, w, h)
    float4   g_SSAOParams;      // (radius, bias, power, intensity)
    float4   g_SSAOOptions;     // (sample count, unused, unused, unused)
    float4   g_Samples[64];     // hemisphere kernel (xyz, 0)
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
    // pos maps uv (0,0)->top-left (0,1)->bottom-left (1,0)->top-right
    o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// Reconstruct view-space position from depth.
// uv.y == 0 at screen TOP   (NDC y = +1)
// uv.y == 1 at screen BOTTOM (NDC y = -1)
float3 ViewPosFromDepth(float2 uv, float depth, float4x4 invProj)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 c = float4(ndc, depth, 1.0f);
    float4 v = mul(c, invProj);
    return v.xyz / v.w;
}

// Reconstruct view-space normal using central differences.
float3 ViewNormalFromDepth(float2 uv, float2 ts, float4x4 invProj)
{
    float dL = g_DepthTex.SampleLevel(g_DepthSampler, uv + float2(-ts.x,  0.0f), 0).r;
    float dR = g_DepthTex.SampleLevel(g_DepthSampler, uv + float2( ts.x,  0.0f), 0).r;
    float dD = g_DepthTex.SampleLevel(g_DepthSampler, uv + float2( 0.0f, -ts.y), 0).r;
    float dU = g_DepthTex.SampleLevel(g_DepthSampler, uv + float2( 0.0f,  ts.y), 0).r;

    float3 pL = ViewPosFromDepth(uv + float2(-ts.x,  0.0f), dL, invProj);
    float3 pR = ViewPosFromDepth(uv + float2( ts.x,  0.0f), dR, invProj);
    float3 pD = ViewPosFromDepth(uv + float2( 0.0f, -ts.y), dD, invProj);
    float3 pU = ViewPosFromDepth(uv + float2( 0.0f,  ts.y), dU, invProj);

    // pR-pL is screen-right, pU-pD is screen-down.
    // In view space: X right, Y up.  Screen-down in uv = moving toward larger uv.y.
    // With the corrected ndc, pU (larger uv.y) has smaller ndc.y -> lower in view.
    // So pU-pD points downward in view space.  cross(right, down) = -forward.
    // Normal should point toward camera (-Z in LH).  Let cross decide naturally.
    return normalize(cross(pR - pL, pU - pD));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float depth = g_DepthTex.SampleLevel(g_DepthSampler, input.uv, 0).r;

    // sky / far-clip -> fully lit
    if (depth >= 0.9999f)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);

    float2 ts     = g_ScreenSize.xy;
    float3 pos    = ViewPosFromDepth(input.uv, depth, g_InvProjection);
    float3 normal = ViewNormalFromDepth(input.uv, ts, g_InvProjection);

    float radius = g_SSAOParams.x;
    float bias   = g_SSAOParams.y;

    // random rotation vector (noise is [0,1], remap to [-1,1])
    float2 noiseUV = input.uv * (g_ScreenSize.zw / 4.0f);
    float3 rvec    = g_NoiseTex.SampleLevel(g_NoiseSampler, noiseUV, 0).xyz;
    rvec           = normalize(rvec * 2.0f - 1.0f);

    // TBN: Z = normal
    float3 T = normalize(rvec - normal * dot(rvec, normal));
    float3 B = cross(normal, T);

    float occ = 0.0f;
    const uint sampleCount = clamp((uint)round(g_SSAOOptions.x), 1u, 64u);

    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        // tangent-space sample -> view-space offset
        float3 dir    = g_Samples[i].xyz;
        float3 offset = T * dir.x + B * dir.y + normal * dir.z;
        float3 sPos   = pos + offset * radius;

        // project to screen
        float4 sClip = mul(float4(sPos, 1.0f), g_Projection);
        float2 sNdc  = sClip.xy / sClip.w;
        float2 sUv   = float2(sNdc.x * 0.5f + 0.5f, 0.5f - sNdc.y * 0.5f);
        sUv          = clamp(sUv, 0.0f, 1.0f);

        float  sDepth    = g_DepthTex.SampleLevel(g_DepthSampler, sUv, 0).r;
        float3 sViewPos  = ViewPosFromDepth(sUv, sDepth, g_InvProjection);

        // range weight: fall off with distance from centre pixel
        float rangeW = 1.0f - smoothstep(0.0f, radius, abs(pos.z - sViewPos.z));

        // occlusion test: geometry in front of the sample point -> occludes
        // LH view space: +Z forward -> closer = smaller Z
        float occluded = (sViewPos.z < sPos.z - bias) ? 1.0f : 0.0f;
        occ += rangeW * occluded;
    }

    occ  = 1.0f - occ / float(sampleCount);   // visibility: 1=open, 0=blocked
    occ  = pow(max(occ, 0.0f), g_SSAOParams.z); // sharpen
    occ  = lerp(1.0f, occ, saturate(g_SSAOParams.w));

    return float4(occ, occ, occ, 1.0f);
}
