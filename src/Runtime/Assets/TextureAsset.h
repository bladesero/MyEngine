#pragma once

#include "Assets/Asset.h"
#include <vector>
#include <cstdint>
#include <string>

// ==========================================================================
// TextureAsset  –  2D 纹理资产
//
// CPU 侧保存原始 RGBA8 像素数据；GPU 侧句柄由具体渲染后端填充。
// ==========================================================================

enum class TextureFormat : uint8_t {
    RGBA8   = 0,   // 4 bytes per pixel, 8 bits per channel
    RGB8,          // 3 bytes per pixel
    R8,            // 1 byte per pixel (grayscale / roughness)
    BC1,           // DXT1  compressed
    BC3,           // DXT5  compressed
    BC5,           // RGTC2 compressed (normal maps)
};

enum class TextureFilter : uint8_t {
    Nearest,
    Linear,
    Trilinear,
    Anisotropic,
};

enum class TextureWrap : uint8_t {
    Repeat,
    Clamp,
    Mirror,
};

struct TextureDesc {
    int           width    = 0;
    int           height   = 0;
    int           mipLevels = 1;     // 1 = no mips
    TextureFormat format   = TextureFormat::RGBA8;
    TextureFilter filter   = TextureFilter::Linear;
    TextureWrap   wrapU    = TextureWrap::Repeat;
    TextureWrap   wrapV    = TextureWrap::Repeat;
    bool          sRGB     = true;   // gamma-correct sampling
    bool          generateCompressedMips = false;
};

struct TextureMipData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba8;
    std::vector<uint8_t> bc1;
};

class TextureAsset : public Asset {
public:
    explicit TextureAsset(const std::string& path)
        : Asset(AssetType::Texture, path) {}

    // ---- CPU 数据 ----------------------------------------------------------
    void SetPixelData(std::vector<uint8_t> data, const TextureDesc& desc) {
        m_PixelData = std::move(data);
        m_Desc      = desc;
        m_DeferredPayloadPath.clear();
        RebuildDerivedData();
        SetState(AssetState::Ready);
    }

    void SetPixelDataWithMips(std::vector<uint8_t> data,
                              const TextureDesc& desc,
                              std::vector<TextureMipData> mips) {
        if (mips.empty()) {
            SetPixelData(std::move(data), desc);
            return;
        }
        m_PixelData = std::move(data);
        m_Desc = desc;
        m_Mips = std::move(mips);
        m_DeferredPayloadPath.clear();
        if (m_PixelData.empty() && !m_Mips.empty()) {
            m_PixelData = m_Mips.front().rgba8;
        }
        m_Desc.mipLevels = static_cast<int>(m_Mips.size());
        SetState(AssetState::Ready);
    }

    void SetDeferredPayload(std::string payloadPath, const TextureDesc& desc) {
        m_PixelData.clear();
        m_Mips.clear();
        m_Desc = desc;
        m_DeferredPayloadPath = std::move(payloadPath);
        SetState(AssetState::Ready);
    }

    bool EnsurePayloadLoaded();
    bool HasDeferredPayload() const { return !m_DeferredPayloadPath.empty(); }
    bool IsPayloadResident() const { return !m_Mips.empty(); }
    const std::string& GetDeferredPayloadPath() const { return m_DeferredPayloadPath; }

    const std::vector<uint8_t>& GetPixelData() const { return m_PixelData; }
    const TextureDesc&          GetDesc()       const { return m_Desc; }
    const std::vector<TextureMipData>& GetMips() const { return m_Mips; }
    const std::vector<uint8_t>& GetCompressedMip(size_t level) const {
        static const std::vector<uint8_t> empty;
        return level < m_Mips.size() ? m_Mips[level].bc1 : empty;
    }
    void GenerateCompressedMips();

    int  GetWidth()     const { return m_Desc.width;  }
    int  GetHeight()    const { return m_Desc.height; }
    int  GetMipLevels() const { return m_Desc.mipLevels; }
    TextureFormat GetFormat() const { return m_Desc.format; }
    TextureFilter GetFilter() const { return m_Desc.filter; }
    TextureWrap GetWrapU() const { return m_Desc.wrapU; }
    TextureWrap GetWrapV() const { return m_Desc.wrapV; }
    void SetSampler(TextureFilter filter, TextureWrap wrapU, TextureWrap wrapV) {
        m_Desc.filter = filter;
        m_Desc.wrapU = wrapU;
        m_Desc.wrapV = wrapV;
    }

    // ---- GPU 句柄（由渲染后端填写，类型擦除为 void*）----------------------
    // 具体后端（D3D11/Vulkan 等）转换时自行 reinterpret_cast
    void  SetGpuHandle(void* handle) { m_GpuHandle = handle; }
    void* GetGpuHandle()       const { return m_GpuHandle; }
    bool  HasGpuHandle()       const { return m_GpuHandle != nullptr; }

    bool ReloadFrom(const Asset& source) override {
        const auto* texture = dynamic_cast<const TextureAsset*>(&source);
        if (!texture) return false;
        if (texture->HasDeferredPayload() && !texture->IsPayloadResident()) {
            SetDeferredPayload(texture->m_DeferredPayloadPath, texture->m_Desc);
            m_GpuHandle = nullptr;
            return true;
        }
        SetPixelDataWithMips(texture->m_PixelData, texture->m_Desc,
                             texture->m_Mips);
        m_GpuHandle = nullptr;
        return true;
    }

    // ---- 工厂：创建纯色占位纹理（1×1）--------------------------------------
    static std::shared_ptr<TextureAsset> CreateSolid(
        const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        auto tex = std::make_shared<TextureAsset>("__builtin__/" + name);
        tex->SetName(name);
        TextureDesc desc;
        desc.width  = 1;
        desc.height = 1;
        tex->SetPixelData({ r, g, b, a }, desc);
        return tex;
    }

private:
    void RebuildDerivedData();

    TextureDesc          m_Desc;
    std::vector<uint8_t> m_PixelData;
    std::vector<TextureMipData> m_Mips;
    std::string          m_DeferredPayloadPath;
    void*                m_GpuHandle = nullptr;
};

using TextureHandle = AssetHandle<TextureAsset>;

bool SaveTexturePayloadToFile(const TextureAsset& texture, const std::string& path);
