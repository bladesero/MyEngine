#include "Renderer/ShaderManager.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Renderer/ShaderCacheService.h"
#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#endif
#include "Renderer/ShaderCompilerSlang.h"
#include <fstream>
#include <sstream>

ShaderManager& ShaderManager::Get() {
    static ShaderManager instance;
    return instance;
}

std::string ShaderManager::MakeKey(const ShaderAsset& asset, const VertexElement* layout, uint32_t count,
                                   bool compute) const {
    std::ostringstream out;
    out << asset.GetID() << ':' << asset.GetVersion() << ':'
        << static_cast<int>(m_Device ? m_Device->GetBackend() : RHIBackend::Unknown) << ':' << compute;
    for (uint32_t i = 0; i < count; ++i) {
        out << '|' << layout[i].semantic << ':' << layout[i].index << ':' << static_cast<int>(layout[i].format) << ':'
            << layout[i].offset;
    }
    return out.str();
}

std::shared_ptr<GpuShader> ShaderManager::CompileRecord(const ShaderRecord& rec) {
    if (!m_Device || !rec.asset.IsValid())
        return {};
    const ShaderAsset& asset = *rec.asset;
    const RHIBackend activeBackend = m_Device->GetBackend();
    const ShaderBackend backend =
        activeBackend == RHIBackend::Metal
            ? ShaderBackend::Metal
            : (activeBackend == RHIBackend::Vulkan
                   ? ShaderBackend::Vulkan
                   : (activeBackend == RHIBackend::D3D12 ? ShaderBackend::D3D12 : ShaderBackend::D3D11));
    auto createFromCooked = [&](const ShaderAsset& cooked) -> std::shared_ptr<GpuShader> {
        if (rec.compute) {
            const auto& cs = cooked.GetBytecode(backend, ShaderStage::Compute);
            return cs.empty() ? nullptr : m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size());
        }
        const auto& vs = cooked.GetBytecode(backend, ShaderStage::Vertex);
        const auto& ps = cooked.GetBytecode(backend, ShaderStage::Pixel);
        if (vs.empty() || ps.empty())
            return {};
        return m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                  static_cast<uint32_t>(rec.layout.size()));
    };
    if (asset.IsCooked()) {
        return createFromCooked(asset);
    }

    const bool allowCompile = m_CacheMode == ShaderCacheMode::EditorOnDemandCompile;
    ShaderCacheRequest cacheRequest;
    cacheRequest.sourcePath = rec.path;
    cacheRequest.backends = {backend};
    cacheRequest.allowCompile = allowCompile;
    const ShaderCacheResult cacheResult = ShaderCacheService::Get().EnsureShaderArtifact(cacheRequest);
    if (cacheResult.succeeded && !cacheResult.artifactPath.empty()) {
        if (auto cooked = LoadShaderAssetFromFile(cacheResult.artifactPath.string()); cooked && cooked->IsCooked()) {
            if (auto shader = createFromCooked(*cooked))
                return shader;
            Logger::Error("[ShaderManager] Cached shader is missing backend bytecode: ",
                          cacheResult.artifactPath.string());
        } else {
            Logger::Error("[ShaderManager] Shader cache artifact is invalid: ", cacheResult.artifactPath.string());
        }
    }
    if (!allowCompile) {
        Logger::Error("[ShaderManager] Missing cooked shader cache for runtime-only mode: ", rec.path,
                      cacheResult.diagnostic.empty() ? std::string{} : " (", cacheResult.diagnostic,
                      cacheResult.diagnostic.empty() ? std::string{} : ")");
        return {};
    }
    if (activeBackend == RHIBackend::Metal || activeBackend == RHIBackend::Vulkan) {
        if (rec.compute) {
            std::vector<uint8_t> cs;
            std::string error;
            const auto& stage = asset.GetStage(ShaderStage::Compute);
            if (!ShaderCompilerSlang::CompileStageFromFile(asset.ResolveSource(ShaderStage::Compute), stage.entry,
                                                           ShaderStage::Compute, backend, cs, asset.GetDefines(),
                                                           &error)) {
                Logger::Error("[ShaderManager] ", error);
                return {};
            }
            return m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size());
        }
        std::vector<uint8_t> vs, ps;
        std::string error;
        const auto& vsStage = asset.GetStage(ShaderStage::Vertex);
        const auto& psStage = asset.GetStage(ShaderStage::Pixel);
        if (!ShaderCompilerSlang::CompileStageFromFile(asset.ResolveSource(ShaderStage::Vertex), vsStage.entry,
                                                       ShaderStage::Vertex, backend, vs, asset.GetDefines(), &error) ||
            !ShaderCompilerSlang::CompileStageFromFile(asset.ResolveSource(ShaderStage::Pixel), psStage.entry,
                                                       ShaderStage::Pixel, backend, ps, asset.GetDefines(), &error)) {
            Logger::Error("[ShaderManager] ", error);
            return {};
        }
        return m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                  static_cast<uint32_t>(rec.layout.size()));
    }
#ifndef MYENGINE_PLATFORM_WINDOWS
    if (activeBackend != RHIBackend::Metal) {
        if (rec.compute) {
            const uint8_t dummyBytecode = 0;
            return m_Device->CreateComputeShaderFromBytecode(&dummyBytecode, sizeof(dummyBytecode));
        }
        std::ostringstream source;
        const auto& vsStage = asset.GetStage(ShaderStage::Vertex);
        const auto& psStage = asset.GetStage(ShaderStage::Pixel);
        for (ShaderStage stage : {ShaderStage::Vertex, ShaderStage::Pixel}) {
            std::ifstream input(asset.ResolveSource(stage), std::ios::binary);
            if (input)
                source << input.rdbuf() << '\n';
        }
        return m_Device->CreateShader(source.str(), vsStage.entry, psStage.entry, rec.layout.data(),
                                      static_cast<uint32_t>(rec.layout.size()));
    }
#endif
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (rec.compute) {
        std::vector<unsigned char> cs;
        const auto& stage = asset.GetStage(ShaderStage::Compute);
        const char* profile = backend == ShaderBackend::D3D12 ? "cs_5_1" : "cs_5_0";
        const bool ok =
            backend == ShaderBackend::D3D12
                ? ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Compute).string(),
                                                            stage.entry, profile, cs, asset.GetDefines())
                : ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Compute).string(),
                                                            stage.entry, profile, cs, asset.GetDefines());
        return ok ? m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size()) : nullptr;
    }
    const auto& vsStage = asset.GetStage(ShaderStage::Vertex);
    const auto& psStage = asset.GetStage(ShaderStage::Pixel);
    std::vector<unsigned char> vs, ps;
    const bool ok =
        backend == ShaderBackend::D3D12
            ? ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Vertex).string(),
                                                        vsStage.entry, "vs_5_1", vs, asset.GetDefines()) &&
                  ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Pixel).string(),
                                                            psStage.entry, "ps_5_1", ps, asset.GetDefines())
            : ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Vertex).string(),
                                                        vsStage.entry, "vs_5_0", vs, asset.GetDefines()) &&
                  ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Pixel).string(),
                                                            psStage.entry, "ps_5_0", ps, asset.GetDefines());
    return ok ? m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                   static_cast<uint32_t>(rec.layout.size()))
              : nullptr;
#else
    return {};
#endif
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateInternal(const std::string& path, const VertexElement* layout,
                                                                 uint32_t count, bool compute) {
    auto asset = AssetManager::Get().Load<ShaderAsset>(path);
    if (!asset.IsValid() || asset->IsCompute() != compute) {
        Logger::Error("[ShaderManager] Invalid shader asset/type: ", path);
        return {};
    }
    const std::string key = MakeKey(*asset, layout, count, compute);
    if (auto it = m_KeyToIndex.find(key); it != m_KeyToIndex.end())
        return m_Records[it->second].handle;
    ShaderRecord rec;
    rec.key = key;
    rec.path = path;
    rec.asset = asset;
    rec.compute = compute;
    if (layout && count)
        rec.layout.assign(layout, layout + count);
    rec.handle = std::make_shared<ShaderHandle>();
    rec.handle->shader = CompileRecord(rec);
    if (rec.handle->shader)
        ++rec.handle->version;
    else
        Logger::Error("[ShaderManager] Initial compile failed: ", path);
    const size_t index = m_Records.size();
    m_Records.push_back(std::move(rec));
    m_KeyToIndex[key] = index;
    return m_Records.back().handle;
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreate(const std::string& path, const VertexElement* layout,
                                                         uint32_t count) {
    return GetOrCreateInternal(path, layout, count, false);
}
std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateCompute(const std::string& path) {
    return GetOrCreateInternal(path, nullptr, 0, true);
}

void ShaderManager::Recompile(const std::string& path) {
    if (!path.empty())
        AssetManager::Get().Reload(path);
    for (auto& rec : m_Records) {
        if (!path.empty() && rec.path != path)
            continue;
        if (path.empty())
            AssetManager::Get().Reload(rec.path);
        auto refreshed = AssetManager::Get().Load<ShaderAsset>(rec.path);
        if (refreshed.IsValid())
            rec.asset = refreshed;
        auto shader = CompileRecord(rec);
        if (!shader) {
            Logger::Error("[ShaderManager] Recompile failed; retained old shader: ", rec.path);
            continue;
        }
        rec.handle->shader = std::move(shader);
        ++rec.handle->version;
    }
}
void ShaderManager::Clear() {
    m_Records.clear();
    m_KeyToIndex.clear();
    m_Device = nullptr;
}
