#pragma once

#include "Assets/Asset.h"
#include "Assets/ShaderGraph.h"

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
    static constexpr uint32_t kDescriptionVersion = 2;
    static constexpr uint32_t kLegacyDescriptionVersion = 1;
    static constexpr uint32_t kCookedFormatVersion = 4;
    static constexpr uint32_t kCookedFormatVersionWithPasses = 4;
    static constexpr uint32_t kCookedFormatVersionWithVulkan = 3;
    static constexpr uint32_t kCookedFormatVersionWithMetal = 2;
    static constexpr uint32_t kLegacyCookedFormatVersion = 1;
    static constexpr uint32_t kFormatVersion = kDescriptionVersion;
    static constexpr uint32_t kVertexMask = 1u << 0;
    static constexpr uint32_t kPixelMask = 1u << 1;
    static constexpr uint32_t kComputeMask = 1u << 2;

    explicit ShaderAsset(const std::string& path) : Asset(AssetType::Shader, path) {}

    bool IsCooked() const { return m_Cooked; }
    bool IsCompute() const { return m_StageMask == kComputeMask; }
    bool IsGraph() const { return m_SourceMode == ShaderSourceMode::Graph; }
    ShaderSourceMode GetSourceMode() const { return m_SourceMode; }
    ShaderDomain GetDomain() const { return m_Domain; }
    ShaderShadingModel GetShadingModel() const { return m_ShadingModel; }
    ShaderSurfaceType GetSurfaceType() const { return m_SurfaceType; }
    uint32_t GetStageMask() const { return m_StageMask; }
    uint32_t GetPassMask() const { return m_PassMask; }
    bool HasPass(ShaderPass pass) const { return (m_PassMask & (1u << static_cast<uint32_t>(pass))) != 0; }
    uint64_t GetSourceHash() const { return m_SourceHash; }
    const ShaderStageSource& GetStage(ShaderStage stage) const { return GetPassStage(ShaderPass::Default, stage); }
    const ShaderStageSource& GetPassStage(ShaderPass pass, ShaderStage stage) const {
        return m_PassSources[static_cast<size_t>(pass)][static_cast<size_t>(stage)];
    }
    const std::vector<std::string>& GetDefines() const { return m_Defines; }
    const std::vector<ShaderPropertyDesc>& GetProperties() const { return m_Properties; }
    const ShaderPropertyDesc* FindPropertyById(const std::string& id) const;
    const ShaderPropertyDesc* FindPropertyByName(const std::string& name) const;
    const ShaderGraph& GetGraph() const { return m_Graph; }
    const std::vector<ShaderGraphDiagnostic>& GetDiagnostics() const { return m_Diagnostics; }
    const std::vector<uint8_t>& GetBytecode(ShaderBackend backend, ShaderStage stage) const {
        return GetBytecode(backend, ShaderPass::Default, stage);
    }
    const std::vector<uint8_t>& GetBytecode(ShaderBackend backend, ShaderPass pass, ShaderStage stage) const {
        return m_Bytecode[static_cast<size_t>(backend)][static_cast<size_t>(pass)][static_cast<size_t>(stage)];
    }
    std::filesystem::path ResolveSource(ShaderStage stage) const;
    std::filesystem::path ResolveSource(ShaderPass pass, ShaderStage stage) const;

    void SetDescription(uint32_t stageMask, std::array<ShaderStageSource, 3> sources, std::vector<std::string> defines,
                        uint64_t sourceHash);
    void SetCodeDescription(
        uint32_t passMask,
        std::array<std::array<ShaderStageSource, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)>
            passSources,
        std::vector<std::string> defines, std::vector<ShaderPropertyDesc> properties, ShaderDomain domain,
        uint64_t sourceHash);
    void SetGraphDescription(ShaderGraph graph, std::vector<ShaderPropertyDesc> properties,
                             ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType, uint64_t sourceHash,
                             std::vector<ShaderGraphDiagnostic> diagnostics = {});
    void SetCooked(uint32_t stageMask, uint64_t sourceHash,
                   std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> bytecode);
    void SetCookedPasses(
        uint32_t passMask, uint64_t sourceHash, ShaderSourceMode sourceMode, ShaderDomain domain,
        ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType, std::vector<ShaderPropertyDesc> properties,
        std::array<
            std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)>,
            kShaderBackendCount>
            bytecode);
    void MarkReady() { SetState(AssetState::Ready); }
    bool ReloadFrom(const Asset& source) override;

private:
    bool m_Cooked = false;
    ShaderSourceMode m_SourceMode = ShaderSourceMode::Code;
    ShaderDomain m_Domain = ShaderDomain::Graphics;
    ShaderShadingModel m_ShadingModel = ShaderShadingModel::Lit;
    ShaderSurfaceType m_SurfaceType = ShaderSurfaceType::Opaque;
    uint32_t m_StageMask = 0;
    uint32_t m_PassMask = 0;
    uint64_t m_SourceHash = 0;
    std::array<std::array<ShaderStageSource, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)>
        m_PassSources{};
    std::vector<std::string> m_Defines;
    std::vector<ShaderPropertyDesc> m_Properties;
    ShaderGraph m_Graph;
    std::vector<ShaderGraphDiagnostic> m_Diagnostics;
    std::array<std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, static_cast<size_t>(ShaderPass::Count)>,
               kShaderBackendCount>
        m_Bytecode{};
};

using ShaderAssetHandle = AssetHandle<ShaderAsset>;

std::shared_ptr<ShaderAsset> LoadShaderAssetFromFile(const std::string& path);
bool SaveCookedShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path, std::string* error = nullptr);
