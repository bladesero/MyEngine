// PostProcessFXAA.hlsl - Fullscreen post-process pass with FXAA + tone mapping + SSAO

Texture2D    g_SceneColor   : register(t0);
SamplerState g_Sampler      : register(s0);
Texture2D    g_SSAOMap      : register(t1);
SamplerState g_SSAOSampler  : register(s1);

cbuffer PostProcessParams : register(b0)
{
    float4 g_Params;        // exposure, gamma, toneMapping, vignette
    float4 g_Params2;       // saturation, contrast, fxaaEnabled, fxaaQuality
    float4 g_ScreenSize;    // 1/w, 1/h, w, h
    float4 g_Params3;       // x: input already tone-mapped by Modern compute post; y: composite SSAO enabled
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

float3 AcesToneMap(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 FxaaPass(float2 uv, float2 rcpFrame, float quality)
{
    float3 rgbNW = g_SceneColor.Sample(g_Sampler, uv + float2(-1.0f, -1.0f) * rcpFrame).rgb;
    float3 rgbNE = g_SceneColor.Sample(g_Sampler, uv + float2( 1.0f, -1.0f) * rcpFrame).rgb;
    float3 rgbSW = g_SceneColor.Sample(g_Sampler, uv + float2(-1.0f,  1.0f) * rcpFrame).rgb;
    float3 rgbSE = g_SceneColor.Sample(g_Sampler, uv + float2( 1.0f,  1.0f) * rcpFrame).rgb;
    float3 rgbM  = g_SceneColor.Sample(g_Sampler, uv).rgb;

    float3 luma   = float3(0.299f, 0.587f, 0.114f);
    float  lumaNW = dot(rgbNW, luma);
    float  lumaNE = dot(rgbNE, luma);
    float  lumaSW = dot(rgbSW, luma);
    float  lumaSE = dot(rgbSE, luma);
    float  lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;
    if (range < max(0.0312f, 0.0833f * lumaMax))
        return rgbM;

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float reduceMin = 1.0f / 128.0f;
    const float reduceMul = 1.0f / 8.0f;
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * reduceMul), reduceMin);
    float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    float spanMax = lerp(2.0f, 8.0f, saturate(quality));
    dir = clamp(dir * rcpDirMin, -spanMax, spanMax) * rcpFrame;

    float3 rgbA = 0.5f * (
        g_SceneColor.Sample(g_Sampler, uv + dir * (1.0f / 3.0f - 0.5f)).rgb +
        g_SceneColor.Sample(g_Sampler, uv + dir * (2.0f / 3.0f - 0.5f)).rgb);
    float3 rgbB = rgbA * 0.5f + 0.25f * (
        g_SceneColor.Sample(g_Sampler, uv + dir * -0.5f).rgb +
        g_SceneColor.Sample(g_Sampler, uv + dir *  0.5f).rgb);
    float lumaB = dot(rgbB, luma);

    return (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    if (g_Params3.x > 0.5f)
    {
        // Modern compute post already produced the final output grid. A same-size linear sample is theoretically
        // centered, but a pixel-addressed load makes the final composite phase exact across viewport/backends and
        // prevents the blit from reintroducing a sub-pixel wobble after TAA has converged.
        uint2 textureSize = max((uint2)g_ScreenSize.zw, uint2(1u, 1u));
        uint2 pixel = min((uint2)(input.uv * float2(textureSize)), textureSize - 1u);
        return float4(saturate(g_SceneColor.Load(int3(pixel, 0)).rgb), 1.0f);
    }
    float3 sceneColor = g_SceneColor.Sample(g_Sampler, input.uv).rgb;
    float3 color = sceneColor;

    if (g_Params2.z > 0.5f)
        color = lerp(sceneColor, FxaaPass(input.uv, g_ScreenSize.xy, g_Params2.w), saturate(g_Params2.w));

    if (g_Params3.y > 0.5f)
        color *= g_SSAOMap.Sample(g_SSAOSampler, input.uv).r;

    // Exposure
    color *= max(g_Params.x, 0.0f);

    // Tone mapping
    if (g_Params.z > 0.5f)
        color = AcesToneMap(color);

    // Saturation
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(float3(luminance, luminance, luminance), color, max(g_Params2.x, 0.0f));

    // Contrast
    color = (color - 0.5f) * max(g_Params2.y, 0.0f) + 0.5f;

    // Vignette
    float2 centered = input.uv * 2.0f - 1.0f;
    float  vignette = saturate(1.0f - dot(centered, centered) * g_Params.w);
    color *= vignette;

    color = saturate(color);
    color = pow(color, 1.0f / max(g_Params.y, 0.1f));

    return float4(color, 1.0f);
}
