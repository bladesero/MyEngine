struct GpuSceneObject
{
    row_major float4x4 world;
    row_major float4x4 previousWorld;
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

struct DrawIndexedArgs
{
    uint objectIndex;
    uint indexCount;
    uint instanceCount;
    uint startIndex;
    int baseVertex;
    uint startInstance;
};

cbuffer CullingConstants : register(b0)
{
    row_major float4x4 g_ViewProjection;
    uint g_ObjectCount;
    uint2 g_RenderSize;
    uint g_HiZMipCount;
};

StructuredBuffer<GpuSceneObject> g_Objects : register(t0);
Texture2D<float2> g_HiZ : register(t1);
RWStructuredBuffer<DrawIndexedArgs> g_DrawArgs : register(u0);
RWStructuredBuffer<uint> g_DrawCount : register(u1);

bool IsVisible(GpuSceneObject object)
{
    float3 bmin = object.boundsMin.xyz;
    float3 bmax = object.boundsMax.xyz;
    float4 clips[8];
    [unroll]
    for (uint corner = 0; corner < 8; ++corner)
    {
        float3 p = float3((corner & 1) != 0 ? bmax.x : bmin.x,
                          (corner & 2) != 0 ? bmax.y : bmin.y,
                          (corner & 4) != 0 ? bmax.z : bmin.z);
        clips[corner] = mul(float4(p, 1.0f), g_ViewProjection);
    }
    bool outsideLeft = true, outsideRight = true, outsideBottom = true, outsideTop = true;
    bool outsideNear = true, outsideFar = true;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        outsideLeft = outsideLeft && clips[i].x < -clips[i].w;
        outsideRight = outsideRight && clips[i].x > clips[i].w;
        outsideBottom = outsideBottom && clips[i].y < -clips[i].w;
        outsideTop = outsideTop && clips[i].y > clips[i].w;
        outsideNear = outsideNear && clips[i].z < 0.0f;
        outsideFar = outsideFar && clips[i].z > clips[i].w;
    }
    return !(outsideLeft || outsideRight || outsideBottom || outsideTop || outsideNear || outsideFar);
}

bool IsOccluded(GpuSceneObject object)
{
    float3 bmin = object.boundsMin.xyz;
    float3 bmax = object.boundsMax.xyz;
    float2 ndcMin = float2(1.0f, 1.0f);
    float2 ndcMax = float2(-1.0f, -1.0f);
    float nearestDepth = 1.0f;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner)
    {
        float3 p = float3((corner & 1) != 0 ? bmax.x : bmin.x,
                          (corner & 2) != 0 ? bmax.y : bmin.y,
                          (corner & 4) != 0 ? bmax.z : bmin.z);
        float4 clip = mul(float4(p, 1.0f), g_ViewProjection);
        if (clip.w <= 0.0001f)
            return false;
        float3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        nearestDepth = min(nearestDepth, ndc.z);
    }
    float2 uvMin = saturate(ndcMin * float2(0.5f, -0.5f) + 0.5f);
    float2 uvMax = saturate(ndcMax * float2(0.5f, -0.5f) + 0.5f);
    float2 rectPixels = abs(uvMax - uvMin) * float2(g_RenderSize);
    uint mip = min((uint)max(0.0f, ceil(log2(max(max(rectPixels.x, rectPixels.y), 1.0f)))),
                   g_HiZMipCount - 1);
    uint2 mipSize = max(g_RenderSize >> mip, uint2(1, 1));
    uint2 p0 = min((uint2)(uvMin * mipSize), mipSize - 1);
    uint2 p1 = min((uint2)(float2(uvMax.x, uvMin.y) * mipSize), mipSize - 1);
    uint2 p2 = min((uint2)(float2(uvMin.x, uvMax.y) * mipSize), mipSize - 1);
    uint2 p3 = min((uint2)(uvMax * mipSize), mipSize - 1);
    float farthestOccluder = max(max(g_HiZ.Load(int3(p0, mip)).y, g_HiZ.Load(int3(p1, mip)).y),
                                  max(g_HiZ.Load(int3(p2, mip)).y, g_HiZ.Load(int3(p3, mip)).y));
    return farthestOccluder < 0.999999f && nearestDepth > farthestOccluder + 0.0005f;
}

void EmitDraw(uint objectIndex, GpuSceneObject object)
{
    uint drawIndex;
    InterlockedAdd(g_DrawCount[0], 1, drawIndex);
    DrawIndexedArgs args;
    args.objectIndex = objectIndex;
    args.indexCount = object.indexCount;
    args.instanceCount = 1;
    args.startIndex = object.firstIndex;
    args.baseVertex = object.baseVertex;
    args.startInstance = objectIndex;
    g_DrawArgs[drawIndex] = args;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint objectIndex = dispatchThreadId.x;
    if (objectIndex >= g_ObjectCount)
        return;
    GpuSceneObject object = g_Objects[objectIndex];
    if (!IsVisible(object) || object.indexCount == 0)
        return;
    EmitDraw(objectIndex, object);
}

[numthreads(64, 1, 1)]
void CSOcclusion(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint objectIndex = dispatchThreadId.x;
    if (objectIndex >= g_ObjectCount)
        return;
    GpuSceneObject object = g_Objects[objectIndex];
    if (!IsVisible(object) || object.indexCount == 0 || IsOccluded(object))
        return;
    EmitDraw(objectIndex, object);
}
