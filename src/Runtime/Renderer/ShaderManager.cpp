#include "Renderer/ShaderManager.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Renderer/ShaderCacheService.h"
#include "Renderer/ShaderCooker.h"
#include "Renderer/RHI/PlatformShaderCompiler.h"
#include "Renderer/ShaderCompilerSlang.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace {
std::string CanonicalShaderPath(const std::string& path) {
    std::string alias = std::filesystem::path(path).generic_string();
    const std::string genericAlias = alias;
    if (genericAlias.rfind("EngineContent/", 0) == 0)
        alias = (std::filesystem::path("Content/Engine") / genericAlias.substr(14)).generic_string();
    alias = AssetManager::Get().MakeProjectRelativePath(alias);
    std::error_code error;
    std::filesystem::path resolved = AssetManager::Get().ResolvePath(alias);
    resolved = std::filesystem::absolute(std::move(resolved), error).lexically_normal();
    return (error ? std::filesystem::path(alias).lexically_normal() : resolved).generic_string();
}

std::string PrewarmedArtifactKey(const std::string& path, ShaderBackend backend) {
    return CanonicalShaderPath(path) + "|backend=" + std::to_string(static_cast<int>(backend));
}
} // namespace

ShaderManager& ShaderManager::Get() {
    static ShaderManager instance;
    return instance;
}

ShaderBackend ShaderManager::GetActiveShaderBackend() const {
    const RHIBackend backend = m_Device ? m_Device->GetBackend() : RHIBackend::Unknown;
    if (backend == RHIBackend::D3D12)
        return ShaderBackend::D3D12;
    if (backend == RHIBackend::Vulkan)
        return ShaderBackend::Vulkan;
    if (backend == RHIBackend::Metal)
        return ShaderBackend::Metal;
    return ShaderBackend::D3D11;
}

std::string ShaderManager::MakeKey(const ShaderAsset& asset, ShaderPass pass, const VertexElement* layout,
                                   uint32_t count, bool compute) const {
    std::ostringstream out;
    out << asset.GetID() << ':' << asset.GetVersion() << ':'
        << static_cast<int>(m_Device ? m_Device->GetBackend() : RHIBackend::Unknown) << ':' << compute << ':'
        << static_cast<int>(pass);
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
    const auto mergeStageReflection = [](const CookedShaderStageReflection& metadata,
                                         const std::shared_ptr<GpuShader>& shader, uint8_t stageMask) {
        if (!shader)
            return;
        for (const auto& source : metadata.bindings) {
            auto existing = std::find_if(shader->reflection.bindings.begin(), shader->reflection.bindings.end(),
                                         [&](const ShaderBindingDesc& binding) {
                                             return binding.name == source.name &&
                                                    binding.type == static_cast<ShaderBindingType>(source.type) &&
                                                    binding.bindPoint == source.bindPoint &&
                                                    binding.bindSpace == source.bindSpace;
                                         });
            if (existing != shader->reflection.bindings.end()) {
                existing->stages |= stageMask;
                existing->bindCount = (std::max)(existing->bindCount, source.bindCount);
                existing->byteSize = (std::max)(existing->byteSize, source.byteSize);
                continue;
            }
            ShaderBindingDesc destination;
            destination.name = source.name;
            destination.type = static_cast<ShaderBindingType>(source.type);
            destination.bindPoint = source.bindPoint;
            destination.bindSpace = source.bindSpace;
            destination.bindCount = source.bindCount;
            destination.byteSize = source.byteSize;
            destination.stages = stageMask;
            shader->reflection.bindings.push_back(std::move(destination));
        }
    };
    const auto applyStageReflection = [&](const CookedShaderStageReflection& metadata,
                                          const std::shared_ptr<GpuShader>& shader, ShaderStage stage,
                                          uint8_t stageMask) {
        if (!shader)
            return;
        shader->abiVersion = ShaderAsset::kCookedShaderAbiVersion;
        if (activeBackend != RHIBackend::Vulkan)
            mergeStageReflection(metadata, shader, stageMask);
        if (stage == ShaderStage::Compute) {
            shader->threadGroupSize[0] = metadata.threadGroupSize[0];
            shader->threadGroupSize[1] = metadata.threadGroupSize[1];
            shader->threadGroupSize[2] = metadata.threadGroupSize[2];
        }
    };
    auto applyCookedReflection = [&](const ShaderAsset& cooked, const std::shared_ptr<GpuShader>& shader) {
        if (!shader || cooked.GetCookedShaderAbiVersion() < ShaderAsset::kPreviousCookedShaderAbiVersion)
            return;
        shader->abiVersion = cooked.GetCookedShaderAbiVersion();
        if (activeBackend == RHIBackend::Vulkan) {
            // The Vulkan pipeline layout is created from the final SPIR-V binding decorations. Slang's JSON
            // reflection describes the source-level register model and may not exactly mirror Vulkan's shifted
            // binding numbers, so replacing the native table here can make descriptor writes target a different
            // layout. Keep the authoritative SPIR-V bindings and import only metadata not parsed by the lightweight
            // native reflector.
            if (rec.compute) {
                const auto& metadata = cooked.GetReflection(backend, rec.pass, ShaderStage::Compute);
                shader->threadGroupSize[0] = metadata.threadGroupSize[0];
                shader->threadGroupSize[1] = metadata.threadGroupSize[1];
                shader->threadGroupSize[2] = metadata.threadGroupSize[2];
            }
            return;
        }
        shader->reflection = {};
        const auto mergeStage = [&](ShaderStage stage, uint8_t stageMask) {
            const auto& metadata = cooked.GetReflection(backend, rec.pass, stage);
            mergeStageReflection(metadata, shader, stageMask);
            if (stage == ShaderStage::Compute) {
                shader->threadGroupSize[0] = metadata.threadGroupSize[0];
                shader->threadGroupSize[1] = metadata.threadGroupSize[1];
                shader->threadGroupSize[2] = metadata.threadGroupSize[2];
            }
        };
        if (rec.compute)
            mergeStage(ShaderStage::Compute, ShaderStageCompute);
        else {
            mergeStage(ShaderStage::Vertex, ShaderStageVertex);
            mergeStage(ShaderStage::Pixel, ShaderStagePixel);
        }
    };
    auto createFromCooked = [&](const ShaderAsset& cooked) -> std::shared_ptr<GpuShader> {
        std::shared_ptr<GpuShader> shader;
        if (rec.compute) {
            const auto& cs = cooked.GetBytecode(backend, rec.pass, ShaderStage::Compute);
            shader = cs.empty() ? nullptr : m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size());
        } else {
            const auto& vs = cooked.GetBytecode(backend, rec.pass, ShaderStage::Vertex);
            const auto& ps = cooked.GetBytecode(backend, rec.pass, ShaderStage::Pixel);
            if (!vs.empty() && !ps.empty())
                shader =
                    m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                       static_cast<uint32_t>(rec.layout.size()));
        }
        applyCookedReflection(cooked, shader);
        return shader;
    };
    if (const auto overrideIt = m_CompiledOverrides.find(CanonicalShaderPath(rec.path));
        overrideIt != m_CompiledOverrides.end() && overrideIt->second) {
        return createFromCooked(*overrideIt->second);
    }
    const bool allowCompile = m_CacheMode == ShaderCacheMode::EditorOnDemandCompile;
    if (asset.IsCooked() && !allowCompile) {
        return createFromCooked(asset);
    }

    ShaderCacheRequest cacheRequest;
    cacheRequest.sourcePath = rec.path;
    cacheRequest.backends = {backend};
    cacheRequest.allowCompile = allowCompile;
    ShaderCacheResult cacheResult;
    const auto prewarmed = m_PrewarmedArtifacts.find(PrewarmedArtifactKey(rec.path, backend));
    if (prewarmed != m_PrewarmedArtifacts.end()) {
        cacheResult = prewarmed->second;
        if (!cacheResult.succeeded) {
            Logger::Error("[ShaderManager] Shader prewarm failed for ", rec.path,
                          cacheResult.diagnostic.empty() ? std::string{} : ": ", cacheResult.diagnostic);
            return {};
        }
    } else {
        cacheResult = ShaderCacheService::Get().EnsureShaderArtifact(cacheRequest);
    }
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

    // AssetManager may have resolved rec.path through an old AssetDatabase entry before the Editor
    // cache resolver had a chance to validate it. Direct compilation must use the source descriptor,
    // never that stale cooked container.
    std::shared_ptr<ShaderAsset> sourceAsset;
    const ShaderAsset* compilationAsset = &asset;
    if (asset.IsCooked()) {
        sourceAsset = LoadShaderAssetFromFile(AssetManager::Get().ResolvePath(rec.path));
        if (!sourceAsset || sourceAsset->IsCooked()) {
            Logger::Error("[ShaderManager] Source shader description is unavailable: ", rec.path);
            return {};
        }
        compilationAsset = sourceAsset.get();
    }
    const ShaderAsset& source = *compilationAsset;
    if (activeBackend == RHIBackend::D3D12 || activeBackend == RHIBackend::Metal ||
        activeBackend == RHIBackend::Vulkan) {
        if (rec.compute) {
            std::vector<uint8_t> cs;
            std::string error;
            CookedShaderStageReflection reflection;
            const auto& stage = source.GetPassStage(rec.pass, ShaderStage::Compute);
            if (!ShaderCompilerSlang::CompileStageFromFile(source.ResolveSource(rec.pass, ShaderStage::Compute),
                                                           stage.entry, ShaderStage::Compute, backend, cs,
                                                           source.GetDefines(), &error, &reflection)) {
                Logger::Error("[ShaderManager] ", error);
                return {};
            }
            auto shader = m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size());
            applyStageReflection(reflection, shader, ShaderStage::Compute, ShaderStageCompute);
            return shader;
        }
        std::vector<uint8_t> vs, ps;
        std::string error;
        CookedShaderStageReflection vsReflection;
        CookedShaderStageReflection psReflection;
        const auto& vsStage = source.GetPassStage(rec.pass, ShaderStage::Vertex);
        const auto& psStage = source.GetPassStage(rec.pass, ShaderStage::Pixel);
        if (!ShaderCompilerSlang::CompileStageFromFile(source.ResolveSource(rec.pass, ShaderStage::Vertex),
                                                       vsStage.entry, ShaderStage::Vertex, backend, vs,
                                                       source.GetDefines(), &error, &vsReflection) ||
            !ShaderCompilerSlang::CompileStageFromFile(source.ResolveSource(rec.pass, ShaderStage::Pixel),
                                                       psStage.entry, ShaderStage::Pixel, backend, ps,
                                                       source.GetDefines(), &error, &psReflection)) {
            Logger::Error("[ShaderManager] ", error);
            return {};
        }
        auto shader = m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                         static_cast<uint32_t>(rec.layout.size()));
        if (shader && activeBackend != RHIBackend::Vulkan)
            shader->reflection = {};
        applyStageReflection(vsReflection, shader, ShaderStage::Vertex, ShaderStageVertex);
        applyStageReflection(psReflection, shader, ShaderStage::Pixel, ShaderStagePixel);
        return shader;
    }
#ifndef MYENGINE_PLATFORM_WINDOWS
    if (activeBackend != RHIBackend::Metal) {
        if (rec.compute) {
            const uint8_t dummyBytecode = 0;
            return m_Device->CreateComputeShaderFromBytecode(&dummyBytecode, sizeof(dummyBytecode));
        }
        std::ostringstream sourceText;
        const auto& vsStage = source.GetPassStage(rec.pass, ShaderStage::Vertex);
        const auto& psStage = source.GetPassStage(rec.pass, ShaderStage::Pixel);
        for (ShaderStage stage : {ShaderStage::Vertex, ShaderStage::Pixel}) {
            std::ifstream input(source.ResolveSource(rec.pass, stage), std::ios::binary);
            if (input)
                sourceText << input.rdbuf() << '\n';
        }
        return m_Device->CreateShader(sourceText.str(), vsStage.entry, psStage.entry, rec.layout.data(),
                                      static_cast<uint32_t>(rec.layout.size()));
    }
#endif
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (rec.compute) {
        std::vector<unsigned char> cs;
        const auto& stage = source.GetPassStage(rec.pass, ShaderStage::Compute);
        const char* profile = backend == ShaderBackend::D3D12 ? "cs_5_1" : "cs_5_0";
        const bool ok = CompileD3DShaderStage(backend == ShaderBackend::D3D12,
                                              source.ResolveSource(rec.pass, ShaderStage::Compute).string(),
                                              stage.entry, profile, cs, source.GetDefines());
        return ok ? m_Device->CreateComputeShaderFromBytecode(cs.data(), cs.size()) : nullptr;
    }
    const auto& vsStage = source.GetPassStage(rec.pass, ShaderStage::Vertex);
    const auto& psStage = source.GetPassStage(rec.pass, ShaderStage::Pixel);
    std::vector<unsigned char> vs, ps;
    const bool d3d12 = backend == ShaderBackend::D3D12;
    const bool ok = CompileD3DShaderStage(d3d12, source.ResolveSource(rec.pass, ShaderStage::Vertex).string(),
                                          vsStage.entry, d3d12 ? "vs_5_1" : "vs_5_0", vs, source.GetDefines()) &&
                    CompileD3DShaderStage(d3d12, source.ResolveSource(rec.pass, ShaderStage::Pixel).string(),
                                          psStage.entry, d3d12 ? "ps_5_1" : "ps_5_0", ps, source.GetDefines());
    return ok ? m_Device->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(), rec.layout.data(),
                                                   static_cast<uint32_t>(rec.layout.size()))
              : nullptr;
#else
    return {};
#endif
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateInternal(const std::string& path, ShaderPass pass,
                                                                 const VertexElement* layout, uint32_t count,
                                                                 bool compute) {
    auto asset = AssetManager::Get().Load<ShaderAsset>(path);
    if (!asset.IsValid() || asset->IsCompute() != compute || !asset->HasPass(pass)) {
        Logger::Error("[ShaderManager] Invalid shader asset/type: ", path);
        return {};
    }
    const std::string key = MakeKey(*asset, pass, layout, count, compute);
    if (auto it = m_KeyToIndex.find(key); it != m_KeyToIndex.end())
        return m_Records[it->second].handle;
    ShaderRecord rec;
    rec.key = key;
    rec.path = path;
    rec.asset = asset;
    rec.compute = compute;
    rec.pass = pass;
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
    return GetOrCreateInternal(path, ShaderPass::Default, layout, count, false);
}
std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreatePass(const std::string& path, ShaderPass pass,
                                                             const VertexElement* layout, uint32_t count) {
    return GetOrCreateInternal(path, pass, layout, count, false);
}
std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateCompute(const std::string& path) {
    return GetOrCreateInternal(path, ShaderPass::Default, nullptr, 0, true);
}

std::vector<ShaderCacheRequest> ShaderManager::BuildPrewarmRequests(const std::vector<std::string>& paths,
                                                                    std::vector<std::string>& pendingPaths,
                                                                    ShaderBackend& backend,
                                                                    bool* hasCachedFailures) const {
    backend = GetActiveShaderBackend();
    if (hasCachedFailures)
        *hasCachedFailures = false;
    const bool allowCompile = m_CacheMode == ShaderCacheMode::EditorOnDemandCompile;
    std::unordered_set<std::string> uniquePaths;
    std::vector<ShaderCacheRequest> requests;
    pendingPaths.clear();
    pendingPaths.reserve(paths.size());
    requests.reserve(paths.size());
    for (const std::string& path : paths) {
        if (path.empty())
            continue;
        const std::string canonicalPath = CanonicalShaderPath(path);
        if (!uniquePaths.insert(canonicalPath).second)
            continue;
        const std::string key = PrewarmedArtifactKey(path, backend);
        const auto cached = m_PrewarmedArtifacts.find(key);
        if (cached != m_PrewarmedArtifacts.end()) {
            if (!cached->second.succeeded && hasCachedFailures)
                *hasCachedFailures = true;
            continue;
        }
        pendingPaths.push_back(path);
        // Resolve aliases while the request is still being assembled on the render/main thread. The filesystem
        // cache batch runs asynchronously and must not consult AssetManager's mutable project/registry state.
        requests.push_back(
            {std::filesystem::path(AssetManager::Get().ResolvePath(canonicalPath)), {backend}, allowCompile});
    }
    return requests;
}

bool ShaderManager::StorePrewarmResults(const std::vector<std::string>& pendingPaths, ShaderBackend backend,
                                        std::vector<ShaderCacheResult> results,
                                        std::chrono::steady_clock::time_point start) {
    uint32_t succeeded = 0;
    uint32_t cacheHits = 0;
    for (size_t index = 0; index < pendingPaths.size(); ++index) {
        ShaderCacheResult result;
        if (index < results.size())
            result = std::move(results[index]);
        if (result.succeeded) {
            ++succeeded;
            cacheHits += result.cacheHit ? 1u : 0u;
        } else {
            Logger::Warn("[ShaderManager] Failed to prepare ", pendingPaths[index], ": ",
                         result.diagnostic.empty() ? "unknown shader cache error" : result.diagnostic);
        }
        m_PrewarmedArtifacts[PrewarmedArtifactKey(pendingPaths[index], backend)] = std::move(result);
    }
    const float elapsedMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (succeeded != 0) {
        Logger::Info("[ShaderManager] Prepared ", succeeded, "/", pendingPaths.size(), " ",
                     ShaderCooker::BackendName(backend), " shader artifacts in ", elapsedMs,
                     " ms (cache hits=", cacheHits, ", compiled=", succeeded - cacheHits, ")");
    }
    return succeeded == pendingPaths.size();
}

void ShaderManager::DrainPendingPrewarm(bool storeResults) {
    if (!m_PendingPrewarm)
        return;
    std::vector<ShaderCacheResult> results;
    try {
        results = m_PendingPrewarm->results.get();
    } catch (const std::exception& exception) {
        Logger::Warn("[ShaderManager] Asynchronous shader prewarm failed: ", exception.what());
    } catch (...) {
        Logger::Warn("[ShaderManager] Asynchronous shader prewarm failed with an unknown exception");
    }
    if (storeResults && m_PendingPrewarm->generation == m_PrewarmGeneration) {
        StorePrewarmResults(m_PendingPrewarm->paths, m_PendingPrewarm->backend, std::move(results),
                            m_PendingPrewarm->start);
    }
    m_PendingPrewarm.reset();
}

bool ShaderManager::PrewarmCacheArtifacts(const std::vector<std::string>& paths) {
    DrainPendingPrewarm(true);
    if (!m_Device || paths.empty() || !ShaderCacheService::Get().HasResolver())
        return true;

    ShaderBackend backend = ShaderBackend::D3D11;
    std::vector<std::string> pendingPaths;
    bool hasCachedFailures = false;
    std::vector<ShaderCacheRequest> requests = BuildPrewarmRequests(paths, pendingPaths, backend, &hasCachedFailures);
    if (requests.empty())
        return !hasCachedFailures;
    const auto start = std::chrono::steady_clock::now();
    std::vector<ShaderCacheResult> results = ShaderCacheService::Get().EnsureShaderArtifacts(requests);
    return StorePrewarmResults(pendingPaths, backend, std::move(results), start);
}

ShaderPrewarmStatus ShaderManager::PrewarmCacheArtifactsAsync(const std::vector<std::string>& paths) {
    if (m_PendingPrewarm) {
        if (m_PendingPrewarm->results.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return ShaderPrewarmStatus::Pending;
        const uint64_t generation = m_PendingPrewarm->generation;
        const ShaderBackend backend = m_PendingPrewarm->backend;
        const auto start = m_PendingPrewarm->start;
        std::vector<std::string> pendingPaths = std::move(m_PendingPrewarm->paths);
        std::vector<ShaderCacheResult> results;
        try {
            results = m_PendingPrewarm->results.get();
        } catch (const std::exception& exception) {
            Logger::Warn("[ShaderManager] Asynchronous shader prewarm failed: ", exception.what());
        } catch (...) {
            Logger::Warn("[ShaderManager] Asynchronous shader prewarm failed with an unknown exception");
        }
        m_PendingPrewarm.reset();
        if (generation == m_PrewarmGeneration)
            StorePrewarmResults(pendingPaths, backend, std::move(results), start);
        // The caller can change its required shader set while an older batch is running (for example, when a
        // preview renderer starts before the Scene viewport switches to Modern Deferred). Re-evaluate the current
        // request below instead of treating completion of the older batch as completion of this one.
    }
    if (!m_Device || paths.empty() || !ShaderCacheService::Get().HasResolver())
        return ShaderPrewarmStatus::Ready;

    ShaderBackend backend = ShaderBackend::D3D11;
    std::vector<std::string> pendingPaths;
    bool hasCachedFailures = false;
    std::vector<ShaderCacheRequest> requests = BuildPrewarmRequests(paths, pendingPaths, backend, &hasCachedFailures);
    if (requests.empty())
        return hasCachedFailures ? ShaderPrewarmStatus::Failed : ShaderPrewarmStatus::Ready;
    auto pending = std::make_unique<PendingPrewarm>();
    pending->paths = std::move(pendingPaths);
    pending->backend = backend;
    pending->generation = m_PrewarmGeneration;
    pending->start = std::chrono::steady_clock::now();
    pending->cancellation = std::make_shared<ShaderCacheBatchCancellation>();
    try {
        pending->results =
            std::async(std::launch::async, [requests = std::move(requests), cancellation = pending->cancellation] {
                return ShaderCacheService::Get().EnsureShaderArtifacts(requests, cancellation);
            });
    } catch (const std::exception& exception) {
        Logger::Warn("[ShaderManager] Failed to start asynchronous shader prewarm: ", exception.what());
        for (const std::string& path : pending->paths) {
            ShaderCacheResult failure;
            failure.diagnostic = std::string("failed to start asynchronous shader prewarm: ") + exception.what();
            m_PrewarmedArtifacts[PrewarmedArtifactKey(path, backend)] = std::move(failure);
        }
        return ShaderPrewarmStatus::Failed;
    }
    Logger::Info("[ShaderManager] Preparing ", pending->paths.size(), " ", ShaderCooker::BackendName(backend),
                 " shader artifacts asynchronously");
    m_PendingPrewarm = std::move(pending);
    return ShaderPrewarmStatus::Pending;
}

void ShaderManager::Recompile(const std::string& path) {
    ++m_PrewarmGeneration;
    const std::string canonicalPath = path.empty() ? std::string{} : CanonicalShaderPath(path);
    if (path.empty()) {
        m_CompiledOverrides.clear();
        m_PrewarmedArtifacts.clear();
    } else {
        m_CompiledOverrides.erase(canonicalPath);
        m_PrewarmedArtifacts.erase(PrewarmedArtifactKey(path, GetActiveShaderBackend()));
    }
    if (!path.empty())
        AssetManager::Get().Reload(path);
    for (auto& rec : m_Records) {
        if (!path.empty() && CanonicalShaderPath(rec.path) != canonicalPath)
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

bool ShaderManager::ApplyCompiledArtifact(const std::string& path, const std::string& cookedArtifactPath,
                                          std::string* error) {
    ++m_PrewarmGeneration;
    auto cooked = LoadShaderAssetFromFile(cookedArtifactPath);
    if (!cooked || !cooked->IsCooked()) {
        if (error)
            *error = "compiled shader artifact is invalid";
        return false;
    }

    const std::string canonicalPath = CanonicalShaderPath(path);
    const auto previous = m_CompiledOverrides.find(canonicalPath);
    std::shared_ptr<ShaderAsset> previousAsset = previous == m_CompiledOverrides.end() ? nullptr : previous->second;
    m_CompiledOverrides[canonicalPath] = cooked;
    std::vector<std::pair<ShaderRecord*, std::shared_ptr<GpuShader>>> replacements;
    for (auto& record : m_Records) {
        if (CanonicalShaderPath(record.path) != canonicalPath)
            continue;
        auto shader = CompileRecord(record);
        if (!shader) {
            if (previousAsset)
                m_CompiledOverrides[canonicalPath] = std::move(previousAsset);
            else
                m_CompiledOverrides.erase(canonicalPath);
            if (error)
                *error = "GPU shader creation failed for pass " + std::string(ShaderPassName(record.pass));
            return false;
        }
        replacements.emplace_back(&record, std::move(shader));
    }
    for (auto& [record, shader] : replacements) {
        record->handle->shader = std::move(shader);
        ++record->handle->version;
    }
    return true;
}
void ShaderManager::Clear() {
    ++m_PrewarmGeneration;
    if (m_PendingPrewarm && m_PendingPrewarm->cancellation)
        m_PendingPrewarm->cancellation->RequestCancel();
    DrainPendingPrewarm(false);
    m_Records.clear();
    m_KeyToIndex.clear();
    m_CompiledOverrides.clear();
    m_PrewarmedArtifacts.clear();
    m_Device = nullptr;
}
