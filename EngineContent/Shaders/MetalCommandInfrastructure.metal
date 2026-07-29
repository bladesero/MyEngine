#include <metal_stdlib>
using namespace metal;

struct MyEngineIndexedDraw
{
    uint objectIndex;
    uint indexCount;
    uint instanceCount;
    uint startIndex;
    int baseVertex;
    uint startInstance;
};

struct MyEngineIndirectCommandContainer
{
    command_buffer commands [[id(0)]];
};

kernel void BuildIndexedCommands(device const MyEngineIndexedDraw* draws [[buffer(0)]],
                                 device const uint* drawCount [[buffer(1)]],
                                 device const uint* indexBuffer [[buffer(2)]],
                                 constant uint2& limits [[buffer(3)]],
                                 constant MyEngineIndirectCommandContainer& commandContainer [[buffer(4)]],
                                 device uint2* executionRange [[buffer(5)]],
                                 uint commandIndex [[thread_position_in_grid]])
{
    const uint count = min(drawCount[0], limits.x);
    if (commandIndex == 0)
        executionRange[0] = uint2(0, count);
    if (commandIndex >= count)
        return;
    const MyEngineIndexedDraw draw = draws[commandIndex];
    render_command command(commandContainer.commands, commandIndex);
    command.draw_indexed_primitives(primitive_type::triangle, draw.indexCount,
                                    indexBuffer + draw.startIndex, draw.instanceCount,
                                    draw.baseVertex, draw.startInstance);
}

kernel void ClearStorageBuffer(device uint* destination [[buffer(0)]],
                               constant uint& clearValue [[buffer(1)]],
                               uint wordIndex [[thread_position_in_grid]])
{
    destination[wordIndex] = clearValue;
}
