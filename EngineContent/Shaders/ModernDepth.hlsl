struct GpuSceneObject
{
    row_major float4x4 world;
    row_major float4x4 previousWorld;
    row_major float4x4 normalMatrix;
    float4 boundsMin;
    float4 boundsMax;
    uint meshId;
    uint materialId;
    uint bonePaletteOffset;
    uint flags;
    uint firstIndex;
    uint indexCount;
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

cbuffer DepthViewConstants : register(b0)
{
    row_major float4x4 g_ViewProjection;
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
    float2 uv : TEXCOORD0;
    float alpha : TEXCOORD1;
    nointerpolation uint materialId : TEXCOORD2;
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
    output.uv = input.uv;
    output.alpha = input.color.a;
    output.materialId = object.materialId;
    return output;
}

float SampleBindlessAlpha(uint textureIndex, uint samplerIndex, float2 uv)
{
    switch (samplerIndex) {
    case 1u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointRepeatSampler, uv).a;
    case 2u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)]
            .Sample(g_LinearClampURepeatVSampler, uv)
            .a;
    case 3u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)]
            .Sample(g_PointClampURepeatVSampler, uv)
            .a;
    case 4u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)]
            .Sample(g_LinearRepeatUClampVSampler, uv)
            .a;
    case 5u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)]
            .Sample(g_PointRepeatUClampVSampler, uv)
            .a;
    case 6u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearClampSampler, uv).a;
    case 7u:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_PointClampSampler, uv).a;
    default:
        return g_BindlessTextures[NonUniformResourceIndex(textureIndex)].Sample(g_LinearRepeatSampler, uv).a;
    }
}

void PSMain(VSOutput input, bool frontFace : SV_IsFrontFace)
{
    GpuSceneMaterial material = g_Materials[input.materialId];
    if ((material.flags & (1u << 9u)) == 0 && !frontFace)
        discard;
    if ((material.flags & (1u << 8u)) == 0)
        return;
    float textureAlpha = (material.flags & 1u) != 0
                             ? SampleBindlessAlpha(material.textureIndices0.x, material.samplerIndices0.x, input.uv)
                             : 1.0f;
    clip(textureAlpha * material.baseColor.a * input.alpha - material.material.w);
}
