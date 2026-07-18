cbuffer UIScreenConstants : register(b0)
{
    float2 u_InvSize;
    float2 u_Translation;
};

Texture2D u_Texture : register(t0);
SamplerState u_Sampler : register(s0);

struct VSIn
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    const float2 position = input.position + u_Translation;
    output.position = float4(position.x * u_InvSize.x * 2.0f - 1.0f,
                             1.0f - position.y * u_InvSize.y * 2.0f,
                             0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    return u_Texture.Sample(u_Sampler, input.uv) * input.color;
}
