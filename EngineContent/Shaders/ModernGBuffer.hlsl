struct GpuSceneObject
{
    row_major float4x4 world;
    row_major float4x4 previousWorld;
    row_major float4x4 normalMatrix;
    float4 boundsMin;
    float4 boundsMax;
    uint meshId, materialId, bonePaletteOffset, flags;
    uint firstIndex, indexCount;
    int baseVertex;
    uint objectId;
};

struct GpuSceneMaterial
{
    float4 baseColor;
    float4 material;
    float4 emissive;
    uint4 textureIndices0;
    uint4 samplerIndices0;
    uint textureIndex4;
    uint samplerIndex4;
    uint flags;
    uint padding;
};

cbuffer ModernGBufferConstants : register(b0)
{
    row_major float4x4 g_ViewProjection;
    row_major float4x4 g_PreviousViewProjection;
};

#ifdef MYENGINE_D3D12
cbuffer ModernDrawConstants : register(b1)
{
    uint g_ObjectIndex;
};
#endif

StructuredBuffer<GpuSceneObject> g_Objects : register(t0);
StructuredBuffer<GpuSceneMaterial> g_Materials : register(t1);
SamplerState g_LinearRepeatSampler : register(s0);
SamplerState g_PointRepeatSampler : register(s1);
SamplerState g_LinearClampURepeatVSampler : register(s2);
SamplerState g_PointClampURepeatVSampler : register(s3);
SamplerState g_LinearRepeatUClampVSampler : register(s4);
SamplerState g_PointRepeatUClampVSampler : register(s5);
SamplerState g_LinearClampSampler : register(s6);
SamplerState g_PointClampSampler : register(s7);
Texture2D<float4> g_BindlessTextures[] : register(t0, space1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normalW : NORMAL;
    float3 tangentW : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 currentClip : TEXCOORD1;
    float4 previousClip : TEXCOORD2;
    nointerpolation uint materialId : TEXCOORD3;
};

#ifdef MYENGINE_VULKAN
#define MYENGINE_DRAW_INSTANCE_SEMANTIC SV_VulkanInstanceID
#else
#define MYENGINE_DRAW_INSTANCE_SEMANTIC SV_InstanceID
#endif

VSOutput VSMain(VSInput input, uint drawInstanceIndex : MYENGINE_DRAW_INSTANCE_SEMANTIC)
{
#ifdef MYENGINE_D3D12
    uint objectIndex = g_ObjectIndex;
#else
    uint objectIndex = drawInstanceIndex;
#endif
    GpuSceneObject object = g_Objects[objectIndex];
    VSOutput output;
    float4 world = mul(float4(input.position, 1.0f), object.world);
    output.position = mul(world, g_ViewProjection);
    output.currentClip = output.position;
    output.previousClip = mul(mul(float4(input.position, 1.0f), object.previousWorld), g_PreviousViewProjection);
    output.normalW = normalize(mul(float4(input.normal, 0.0f), object.normalMatrix).xyz);
    float3 tangentW = mul(float4(input.tangent, 0.0f), object.world).xyz;
    tangentW -= output.normalW * dot(tangentW, output.normalW);
    if (dot(tangentW, tangentW) <= 1e-8f)
    {
        float3 fallbackAxis = abs(output.normalW.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f)
                                                              : float3(1.0f, 0.0f, 0.0f);
        tangentW = cross(fallbackAxis, output.normalW);
    }
    output.tangentW = normalize(tangentW);
    output.uv = input.uv;
    output.color = input.color;
    output.materialId = object.materialId;
    return output;
}

struct PSOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 emissive : SV_Target3;
    float2 velocity : SV_Target4;
};

float4 SampleBindless(uint textureIndex, uint samplerIndex, float2 uv)
{
    switch (samplerIndex) {
    case 1u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointRepeatSampler, uv);
    case 2u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearClampURepeatVSampler, uv);
    case 3u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointClampURepeatVSampler, uv);
    case 4u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearRepeatUClampVSampler, uv);
    case 5u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointRepeatUClampVSampler, uv);
    case 6u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearClampSampler, uv);
    case 7u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointClampSampler, uv);
    default:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearRepeatSampler, uv);
    }
}

PSOutput PSMain(VSOutput input, bool frontFace : SV_IsFrontFace)
{
    GpuSceneMaterial material = g_Materials[input.materialId];
    if ((material.flags & (1u << 9u)) == 0 && !frontFace)
        discard;
    float4 baseSample = (material.flags & 1u) != 0
                            ? SampleBindless(material.textureIndices0.x, material.samplerIndices0.x, input.uv)
                                                   : float4(1, 1, 1, 1);
    float alpha = baseSample.a * material.baseColor.a * input.color.a;
    if ((material.flags & (1u << 8u)) != 0 && alpha < material.material.w)
        discard;
    float metallic = material.material.x;
    float roughness = material.material.y;
    float ao = material.material.z;
    if ((material.flags & (1u << 2u)) != 0) {
        float4 mr = SampleBindless(material.textureIndices0.z, material.samplerIndices0.z, input.uv);
        metallic *= mr.b;
        roughness *= mr.g;
    }
    if ((material.flags & (1u << 3u)) != 0)
        ao *= SampleBindless(material.textureIndices0.w, material.samplerIndices0.w, input.uv).r;
    float3 normal = normalize(input.normalW);
    if (!frontFace)
        normal = -normal;
    if ((material.flags & (1u << 1u)) != 0) {
        float3 tangent = normalize(input.tangentW - normal * dot(input.tangentW, normal));
        float3 bitangent = normalize(cross(normal, tangent));
        float3 tangentNormal =
            SampleBindless(material.textureIndices0.y, material.samplerIndices0.y, input.uv).xyz * 2.0f - 1.0f;
        normal = normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normal);
    }
    float3 emissive = material.emissive.rgb;
    if ((material.flags & (1u << 4u)) != 0)
        emissive *= pow(max(SampleBindless(material.textureIndex4, material.samplerIndex4, input.uv).rgb, 0.0f), 2.2f);

    PSOutput output;
    output.albedo = float4(pow(max(baseSample.rgb * material.baseColor.rgb * input.color.rgb, 0.0f), 2.2f), alpha);
    output.normal = float4(normal * 0.5f + 0.5f, 1.0f);
    output.material = float4(saturate(metallic), clamp(roughness, 0.04f, 1.0f), max(ao, 0.0f), 0.0f);
    output.emissive = float4(emissive, 0.0f);
    float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 1e-5f);
    float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 1e-5f);
    output.velocity = (currentNdc - previousNdc) * float2(0.5f, -0.5f);
    return output;
}
