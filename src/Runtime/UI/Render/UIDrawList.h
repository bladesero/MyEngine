#pragma once

#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuTexture.h"

#include <cstdint>
#include <memory>
#include <vector>

struct UIVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct UIScissorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool enabled = false;
};

struct UIDrawCommand {
    std::shared_ptr<GpuBuffer> vertexBuffer;
    std::shared_ptr<GpuBuffer> indexBuffer;
    std::shared_ptr<GpuTexture> texture;
    UIScissorRect scissor;
    uint32_t indexCount = 0;
    uint32_t startIndex = 0;
    uint32_t baseVertex = 0;
    float translateX = 0.0f;
    float translateY = 0.0f;
};

class UIDrawList {
public:
    void Clear() { m_Commands.clear(); }
    void Add(UIDrawCommand command) { m_Commands.push_back(std::move(command)); }
    bool Empty() const { return m_Commands.empty(); }
    size_t Size() const { return m_Commands.size(); }
    const std::vector<UIDrawCommand>& GetCommands() const { return m_Commands; }

private:
    std::vector<UIDrawCommand> m_Commands;
};
