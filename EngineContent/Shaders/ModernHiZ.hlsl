cbuffer HiZConstants : register(b0)
{
    uint2 g_SourceSize;
    uint2 g_DestinationSize;
};

Texture2D<float> g_SourceDepth : register(t0);
Texture2D<float2> g_SourceHiZ : register(t1);
RWTexture2D<float2> g_DestinationHiZ : register(u0);

void ExpandRange(inout float2 range, float2 sampleRange)
{
    range.x = min(range.x, sampleRange.x);
    range.y = max(range.y, sampleRange.y);
}

[numthreads(8, 8, 1)]
void CSInit(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= g_DestinationSize))
        return;
    float depth = g_SourceDepth.Load(int3(dispatchThreadId.xy, 0));
    g_DestinationHiZ[dispatchThreadId.xy] = depth.xx;
}

[numthreads(8, 8, 1)]
void CSReduce(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId.xy >= g_DestinationSize))
        return;
    uint2 source = dispatchThreadId.xy * 2;
    uint2 p0 = min(source, g_SourceSize - 1);
    uint2 p1 = min(source + uint2(1, 0), g_SourceSize - 1);
    uint2 p2 = min(source + uint2(0, 1), g_SourceSize - 1);
    uint2 p3 = min(source + uint2(1, 1), g_SourceSize - 1);
    float2 a = g_SourceHiZ.Load(int3(p0, 0));
    float2 b = g_SourceHiZ.Load(int3(p1, 0));
    float2 c = g_SourceHiZ.Load(int3(p2, 0));
    float2 d = g_SourceHiZ.Load(int3(p3, 0));
    float2 range = float2(min(min(a.x, b.x), min(c.x, d.x)), max(max(a.y, b.y), max(c.y, d.y)));

    // Native mip dimensions round down. For an odd source dimension the final destination texel therefore owns a
    // third source row/column; fold those edge samples into the conservative range instead of dropping them.
    bool includeExtraColumn = g_SourceSize.x > g_DestinationSize.x * 2u &&
                              dispatchThreadId.x + 1u == g_DestinationSize.x;
    bool includeExtraRow = g_SourceSize.y > g_DestinationSize.y * 2u &&
                           dispatchThreadId.y + 1u == g_DestinationSize.y;
    if (includeExtraColumn)
    {
        uint edgeX = g_SourceSize.x - 1u;
        ExpandRange(range, g_SourceHiZ.Load(int3(uint2(edgeX, p0.y), 0)));
        ExpandRange(range, g_SourceHiZ.Load(int3(uint2(edgeX, p2.y), 0)));
    }
    if (includeExtraRow)
    {
        uint edgeY = g_SourceSize.y - 1u;
        ExpandRange(range, g_SourceHiZ.Load(int3(uint2(p0.x, edgeY), 0)));
        ExpandRange(range, g_SourceHiZ.Load(int3(uint2(p1.x, edgeY), 0)));
    }
    if (includeExtraColumn && includeExtraRow)
        ExpandRange(range, g_SourceHiZ.Load(int3(g_SourceSize - 1u, 0)));

    g_DestinationHiZ[dispatchThreadId.xy] = range;
}
