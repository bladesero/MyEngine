#pragma once

#include "Assets/Asset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class ShaderStage : uint8_t { Vertex = 0, Pixel = 1, Compute = 2 };
enum class ShaderBackend : uint8_t { D3D11 = 0, D3D12 = 1, Metal = 2, Vulkan = 3 };
inline constexpr size_t kShaderBackendCount = 4;
inline constexpr size_t kShaderStageCount = 3;

struct ShaderStageSource {
    std::string source;
    std::string entry;
};

class ShaderAsset final : public Asset {
public:
    static constexpr uint32_t kDescriptionVersion = 1;
    static constexpr uint32_t kCookedFormatVersion = 3;
    static constexpr uint32_t kCookedFormatVersionWithMetal = 2;
    static constexpr uint32_t kLegacyCookedFormatVersion = 1;
    static constexpr uint32_t kFormatVersion = kDescriptionVersion;
    static constexpr uint32_t kVertexMask = 1u << 0;
    static constexpr uint32_t kPixelMask = 1u << 1;
    static constexpr uint32_t kComputeMask = 1u << 2;

    explicit ShaderAsset(const std::string& path) : Asset(AssetType::Shader, path) {}

    bool IsCooked() const { return m_Cooked; }
    bool IsCompute() const { return m_StageMask == kComputeMask; }
    uint32_t GetStageMask() const { return m_StageMask; }
    uint64_t GetSourceHash() const { return m_SourceHash; }
    const ShaderStageSource& GetStage(ShaderStage stage) const { return m_Sources[static_cast<size_t>(stage)]; }
    const std::vector<std::string>& GetDefines() const { return m_Defines; }
    const std::vector<uint8_t>& GetBytecode(ShaderBackend backend, ShaderStage stage) const {
        return m_Bytecode[static_cast<size_t>(backend)][static_cast<size_t>(stage)];
    }
    std::filesystem::path ResolveSource(ShaderStage stage) const;

    void SetDescription(uint32_t stageMask, std::array<ShaderStageSource, 3> sources,
                        std::vector<std::string> defines, uint64_t sourceHash);
    void SetCooked(uint32_t stageMask, uint64_t sourceHash,
                   std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> bytecode);
    void MarkReady() { SetState(AssetState::Ready); }
    bool ReloadFrom(const Asset& source) override;

private:
    bool m_Cooked = false;
    uint32_t m_StageMask = 0;
    uint64_t m_SourceHash = 0;
    std::array<ShaderStageSource, 3> m_Sources{};
    std::vector<std::string> m_Defines;
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> m_Bytecode{};
};

using ShaderAssetHandle = AssetHandle<ShaderAsset>;

std::shared_ptr<ShaderAsset> LoadShaderAssetFromFile(const std::string& path);
bool SaveCookedShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path,
                           std::string* error = nullptr);
