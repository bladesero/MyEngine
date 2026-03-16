#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Forward declarations to keep this header light.
class IWindow;

// --------------------------------------------------------------------------
// GpuBuffer  – opaque handle wrapping a GPU vertex/index buffer.
// --------------------------------------------------------------------------
struct GpuBuffer {
    virtual ~GpuBuffer() = default;
};

// --------------------------------------------------------------------------
// GpuShader  – compiled vertex + pixel shader pair.
// --------------------------------------------------------------------------
struct GpuShader {
    virtual ~GpuShader() = default;
};

// --------------------------------------------------------------------------
// VertexElement / InputLayout desc (API-agnostic)
// --------------------------------------------------------------------------
enum class VertexFormat { Float2, Float3, Float4 };

struct VertexElement {
    const char*  semantic;   // e.g. "POSITION", "COLOR"
    unsigned int index;      // semantic index
    VertexFormat format;
    unsigned int offset;     // byte offset inside the vertex struct
};

// --------------------------------------------------------------------------
// IRenderContext  – platform-agnostic render back-end interface.
// --------------------------------------------------------------------------
class IRenderContext {
public:
    virtual ~IRenderContext() = default;

    // Lifecycle ---------------------------------------------------------------
    virtual bool Init(IWindow* window) = 0;
    virtual void Shutdown()            = 0;

    // Per-frame ---------------------------------------------------------------
    virtual void BeginFrame(float r, float g, float b, float a = 1.0f) = 0;
    virtual void EndFrame()  = 0;   // swap chain present

    // Resource creation -------------------------------------------------------
    // Create an immutable vertex buffer from raw bytes.
    virtual std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) = 0;

    // Compile shader from HLSL source (vertex + pixel in same string,
    // separated by entry-point names).
    virtual std::shared_ptr<GpuShader> CreateShader(
        const std::string& hlslSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount) = 0;

    // Draw calls --------------------------------------------------------------
    virtual void BindShader(GpuShader* shader) = 0;
    virtual void BindVertexBuffer(GpuBuffer* buffer) = 0;

    // Upload a small per-draw constant buffer (MVP matrix, 64 bytes).
    virtual void SetVSConstants(const void* data, uint32_t byteSize) = 0;

    virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;

    // Viewport ----------------------------------------------------------------
    virtual void SetViewport(float x, float y, float w, float h) = 0;
};

// Factory
std::unique_ptr<IRenderContext> CreateD3D11Context();
