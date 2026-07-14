#pragma once

#include "UI/Render/UIDrawList.h"

#include <RmlUi/Core/RenderInterface.h>

#include <memory>
#include <unordered_map>

class IRHIDevice;

class RmlRenderInterface final : public Rml::RenderInterface {
public:
    explicit RmlRenderInterface(IRHIDevice* device = nullptr);
    ~RmlRenderInterface() override;

    void SetDevice(IRHIDevice* device) { m_Device = device; }
    void BeginFrame(UIDrawList& drawList);
    void EndFrame();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct Geometry {
        std::shared_ptr<GpuBuffer> vertexBuffer;
        std::shared_ptr<GpuBuffer> indexBuffer;
        uint32_t indexCount = 0;
    };
    struct Texture {
        std::shared_ptr<GpuTexture> texture;
        int width = 0;
        int height = 0;
    };

    Rml::CompiledGeometryHandle NextGeometryHandle();
    Rml::TextureHandle NextTextureHandle();
    std::shared_ptr<GpuTexture> CreateTextureFromRgba(const uint8_t* rgba, int width, int height);

    IRHIDevice* m_Device = nullptr;
    UIDrawList* m_DrawList = nullptr;
    UIScissorRect m_Scissor;
    Rml::Matrix4f m_Transform;
    bool m_HasTransform = false;
    Rml::CompiledGeometryHandle m_NextGeometry = 1;
    Rml::TextureHandle m_NextTexture = 1;
    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> m_Geometry;
    std::unordered_map<Rml::TextureHandle, Texture> m_Textures;
};
