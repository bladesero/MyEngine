#pragma once

#include "API/RuntimeApi.h"

#include "Assets/ShaderAsset.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class ShaderCacheMode { EditorOnDemandCompile, RuntimeCookedOnly };

struct ShaderCacheRequest {
    std::filesystem::path sourcePath;
    std::vector<ShaderBackend> backends;
    bool allowCompile = false;
    std::string settingsJson = "{}";
};

enum class ShaderCacheFailureKind { None, CompilerTimeout, Cancelled };

struct ShaderCacheResult {
    bool succeeded = false;
    bool cacheHit = false;
    std::filesystem::path artifactPath;
    std::string diagnostic;
    ShaderCacheFailureKind failureKind = ShaderCacheFailureKind::None;
};

class ShaderCacheBatchCancellation {
public:
    void RequestCancel() noexcept { m_CancelRequested.store(true, std::memory_order_release); }
    bool IsCancellationRequested() const noexcept { return m_CancelRequested.load(std::memory_order_acquire); }

private:
    std::atomic_bool m_CancelRequested{false};
};

class MYENGINE_RUNTIME_API ShaderCacheService {
public:
    using Resolver = std::function<ShaderCacheResult(const ShaderCacheRequest&)>;
    using BatchResolver = std::function<std::vector<ShaderCacheResult>(const std::vector<ShaderCacheRequest>&)>;
    using CancellableBatchResolver = std::function<std::vector<ShaderCacheResult>(
        const std::vector<ShaderCacheRequest>&, const std::shared_ptr<ShaderCacheBatchCancellation>&)>;

    static ShaderCacheService& Get();

    void SetResolver(Resolver resolver, BatchResolver batchResolver = {});
    void SetResolverWithCancellation(Resolver resolver, CancellableBatchResolver batchResolver);
    void ConfigureFileSystemCache(const std::filesystem::path& cacheRoot, std::string targetPlatform = "windows-x64",
                                  size_t maxConcurrency = 0);
    void ClearResolver();
    bool HasResolver() const;
    ShaderCacheResult EnsureShaderArtifact(const ShaderCacheRequest& request) const;
    std::vector<ShaderCacheResult>
    EnsureShaderArtifacts(const std::vector<ShaderCacheRequest>& requests,
                          const std::shared_ptr<ShaderCacheBatchCancellation>& cancellation = {}) const;

private:
    mutable std::mutex m_ResolverMutex;
    Resolver m_Resolver;
    BatchResolver m_BatchResolver;
    CancellableBatchResolver m_CancellableBatchResolver;
};
