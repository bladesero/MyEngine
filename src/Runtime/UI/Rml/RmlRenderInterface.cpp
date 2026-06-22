#include "UI/Rml/RmlRenderInterface.h"

#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Renderer/RHI/IRHIDevice.h"

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

RmlRenderInterface::RmlRenderInterface(IRHIDevice* device)
    : m_Device(device)
{}

RmlRenderInterface::~RmlRenderInterface() = default;

void RmlRenderInterface::BeginFrame(UIDrawList& drawList)
{
    m_DrawList = &drawList;
    m_DrawList->Clear();
}

void RmlRenderInterface::EndFrame()
{
    m_DrawList = nullptr;
}

Rml::CompiledGeometryHandle RmlRenderInterface::NextGeometryHandle()
{
    while (m_NextGeometry == 0 || m_Geometry.count(m_NextGeometry) != 0) ++m_NextGeometry;
    return m_NextGeometry++;
}

Rml::TextureHandle RmlRenderInterface::NextTextureHandle()
{
    while (m_NextTexture == 0 || m_Textures.count(m_NextTexture) != 0) ++m_NextTexture;
    return m_NextTexture++;
}

Rml::CompiledGeometryHandle RmlRenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices)
{
    if (!m_Device || vertices.empty() || indices.empty()) return 0;

    std::vector<UIVertex> uiVertices;
    uiVertices.reserve(vertices.size());
    for (const Rml::Vertex& vertex : vertices) {
        UIVertex out;
        out.x = vertex.position.x;
        out.y = vertex.position.y;
        out.u = vertex.tex_coord.x;
        out.v = vertex.tex_coord.y;
        out.r = static_cast<float>(vertex.colour.red) / 255.0f;
        out.g = static_cast<float>(vertex.colour.green) / 255.0f;
        out.b = static_cast<float>(vertex.colour.blue) / 255.0f;
        out.a = static_cast<float>(vertex.colour.alpha) / 255.0f;
        uiVertices.push_back(out);
    }

    std::vector<uint32_t> uiIndices;
    uiIndices.reserve(indices.size());
    for (int index : indices) uiIndices.push_back(static_cast<uint32_t>(std::max(0, index)));

    Geometry geometry;
    geometry.vertexBuffer = m_Device->CreateVertexBuffer(
        uiVertices.data(),
        static_cast<uint32_t>(uiVertices.size() * sizeof(UIVertex)),
        sizeof(UIVertex));
    geometry.indexBuffer = m_Device->CreateIndexBuffer(
        uiIndices.data(),
        static_cast<uint32_t>(uiIndices.size() * sizeof(uint32_t)));
    geometry.indexCount = static_cast<uint32_t>(uiIndices.size());
    if (!geometry.vertexBuffer || !geometry.indexBuffer) {
        Logger::Warn("[UI] Failed to upload Rml geometry");
        return 0;
    }

    const auto handle = NextGeometryHandle();
    m_Geometry.emplace(handle, std::move(geometry));
    return handle;
}

void RmlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometryHandle,
                                        Rml::Vector2f translation,
                                        Rml::TextureHandle textureHandle)
{
    if (!m_DrawList) return;
    const auto geometryIt = m_Geometry.find(geometryHandle);
    if (geometryIt == m_Geometry.end()) return;

    UIDrawCommand command;
    command.vertexBuffer = geometryIt->second.vertexBuffer;
    command.indexBuffer = geometryIt->second.indexBuffer;
    command.indexCount = geometryIt->second.indexCount;
    command.scissor = m_Scissor;
    command.translateX = translation.x;
    command.translateY = translation.y;
    if (const auto textureIt = m_Textures.find(textureHandle); textureIt != m_Textures.end()) {
        command.texture = textureIt->second.texture;
    }
    (void)translation;
    m_DrawList->Add(std::move(command));
}

void RmlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
    m_Geometry.erase(geometry);
}

std::shared_ptr<GpuTexture> RmlRenderInterface::CreateTextureFromRgba(
    const uint8_t* rgba, int width, int height)
{
    if (!m_Device || !rgba || width <= 0 || height <= 0) return nullptr;
    return m_Device->UploadTexture2D(rgba, width, height);
}

Rml::TextureHandle RmlRenderInterface::LoadTexture(Rml::Vector2i& textureDimensions,
                                                  const Rml::String& source)
{
    auto texture = AssetManager::Get().Load<TextureAsset>(source);
    if (!texture.IsValid()) return 0;
    textureDimensions = {texture->GetWidth(), texture->GetHeight()};
    auto gpuTexture = CreateTextureFromRgba(
        texture->GetPixelData().data(), texture->GetWidth(), texture->GetHeight());
    if (!gpuTexture) return 0;
    const auto handle = NextTextureHandle();
    m_Textures.emplace(handle, Texture{gpuTexture, texture->GetWidth(), texture->GetHeight()});
    return handle;
}

Rml::TextureHandle RmlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                       Rml::Vector2i sourceDimensions)
{
    const int width = sourceDimensions.x;
    const int height = sourceDimensions.y;
    if (source.empty() || width <= 0 || height <= 0) return 0;
    auto gpuTexture = CreateTextureFromRgba(
        reinterpret_cast<const uint8_t*>(source.data()), width, height);
    if (!gpuTexture) return 0;
    const auto handle = NextTextureHandle();
    m_Textures.emplace(handle, Texture{gpuTexture, width, height});
    return handle;
}

void RmlRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
    m_Textures.erase(texture);
}

void RmlRenderInterface::EnableScissorRegion(bool enable)
{
    m_Scissor.enabled = enable;
}

void RmlRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
    m_Scissor.x = region.Left();
    m_Scissor.y = region.Top();
    m_Scissor.width = region.Width();
    m_Scissor.height = region.Height();
}

void RmlRenderInterface::SetTransform(const Rml::Matrix4f* transform)
{
    m_HasTransform = transform != nullptr;
    if (transform) m_Transform = *transform;
}
