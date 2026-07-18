#include "Renderer/ShaderCacheService.h"

#include "Assets/AssetManager.h"
#include "Renderer/ShaderCooker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;

fs::path FindShaderContentRoot(fs::path sourcePath) {
    std::error_code error;
    sourcePath = fs::absolute(std::move(sourcePath), error).lexically_normal();
    fs::path cursor = sourcePath.parent_path();
    while (!cursor.empty()) {
        const std::string name = cursor.filename().string();
        if (name == "Content" || name == "EngineContent")
            return cursor;
        const fs::path parent = cursor.parent_path();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    return sourcePath.parent_path();
}

fs::path ResolveShaderSource(const fs::path& sourcePath) {
    if (sourcePath.is_absolute())
        return sourcePath.lexically_normal();
    return fs::path(AssetManager::Get().ResolvePath(sourcePath.string())).lexically_normal();
}

fs::path MakeStagingArtifactPath(const fs::path& cacheRoot, const std::string& cacheKey) {
    static std::atomic_uint64_t sequence{0};
#if defined(_WIN32)
    const uint64_t process = GetCurrentProcessId();
#else
    const uint64_t process = static_cast<uint64_t>(getpid());
#endif
    // TransactionalFileWriter appends its own unique temporary suffix. Keeping the staging basename compact avoids
    // crossing Win32 MAX_PATH in otherwise ordinary projects (the previous full cache key + PID + TID + timestamp
    // could reach exactly 260 characters before fopen). PID plus the process-local sequence is still collision-free,
    // while the key prefix keeps diagnostics attributable to the requested artifact.
    const std::string keyPrefix = cacheKey.substr(0, (std::min)(cacheKey.size(), static_cast<size_t>(16)));
    return cacheRoot / ".staging" /
           (keyPrefix + "." + std::to_string(process) + "." + std::to_string(++sequence) + ".shader");
}

bool IsCompilerTimeoutFailure(const std::string& diagnostic) {
    // ShaderCompilerSlang emits this explicit marker and skips its ordinary transient-failure retry for a timed-out
    // child process. Do not infer timeout from a slow ordinary compiler diagnostic: those failures must remain
    // independent so the batch can report every real source error.
    return diagnostic.find("slangc timed out") != std::string::npos;
}

ShaderCacheResult CancelledResult(std::string diagnostic = "shader cache batch cancelled before request started") {
    ShaderCacheResult result;
    result.diagnostic = std::move(diagnostic);
    result.failureKind = ShaderCacheFailureKind::Cancelled;
    return result;
}

bool CookedArtifactMatchesRequest(const ShaderAsset& artifact, const ShaderAsset& source,
                                  const std::vector<ShaderBackend>& backends) {
    if (!artifact.IsCooked() || source.IsCooked() ||
        artifact.GetCookedShaderAbiVersion() != ShaderAsset::kCookedShaderAbiVersion ||
        artifact.GetStageMask() != source.GetStageMask() || artifact.GetPassMask() != source.GetPassMask() ||
        artifact.GetSourceHash() != source.GetSourceHash())
        return false;
    for (ShaderBackend backend : backends) {
        bool foundPass = false;
        for (uint32_t passIndex = 0; passIndex < static_cast<uint32_t>(ShaderPass::Count); ++passIndex) {
            const auto pass = static_cast<ShaderPass>(passIndex);
            if (!artifact.HasPass(pass))
                continue;
            foundPass = true;
            if ((artifact.GetStageMask() & ShaderAsset::kComputeMask) != 0) {
                if (artifact.GetBytecode(backend, pass, ShaderStage::Compute).empty())
                    return false;
            } else {
                if ((artifact.GetStageMask() & ShaderAsset::kVertexMask) != 0 &&
                    artifact.GetBytecode(backend, pass, ShaderStage::Vertex).empty())
                    return false;
                if ((artifact.GetStageMask() & ShaderAsset::kPixelMask) != 0 &&
                    artifact.GetBytecode(backend, pass, ShaderStage::Pixel).empty())
                    return false;
            }
        }
        if (!foundPass)
            return false;
    }
    return true;
}

ShaderCacheResult ResolveFileSystemArtifact(const ShaderCacheRequest& request, const fs::path& cacheRoot,
                                            const std::string& targetPlatform,
                                            const std::shared_ptr<ShaderCacheBatchCancellation>& cancellation = {}) {
    ShaderCacheResult result;
    if (request.backends.empty()) {
        result.diagnostic = "filesystem shader cache requires an explicit backend";
        return result;
    }

    const fs::path source = ResolveShaderSource(request.sourcePath);
    const fs::path allowedRoot = FindShaderContentRoot(source);
    constexpr uint32_t kMaxSourceStabilityAttempts = 3;
    for (uint32_t attempt = 0; attempt < kMaxSourceStabilityAttempts; ++attempt) {
        if (cancellation && cancellation->IsCancellationRequested())
            return CancelledResult();
        std::string error;
        const std::string cacheKey = ShaderCooker::BuildCacheKey(source, allowedRoot, request.backends, targetPlatform,
                                                                 request.settingsJson, nullptr, &error);
        if (cacheKey.empty()) {
            result.diagnostic = std::move(error);
            return result;
        }

        const fs::path finalArtifact = cacheRoot / (cacheKey + ".shader");
        result.artifactPath = finalArtifact;
        const auto sourceDescription = LoadShaderAssetFromFile(source.string());
        const auto cached = LoadShaderAssetFromFile(finalArtifact.string());
        if (sourceDescription && cached &&
            CookedArtifactMatchesRequest(*cached, *sourceDescription, request.backends)) {
            result.succeeded = true;
            result.cacheHit = true;
            return result;
        }
        if (!request.allowCompile) {
            result.diagnostic = "shader cache artifact is missing or invalid and compilation is disabled";
            result.artifactPath.clear();
            return result;
        }

        const fs::path stagingArtifact = MakeStagingArtifactPath(cacheRoot, cacheKey);
        ShaderCookRequest cook;
        cook.sourcePath = source;
        cook.artifactPath = stagingArtifact;
        cook.allowedRoot = allowedRoot;
        cook.backends = request.backends;
        cook.targetPlatform = targetPlatform;
        cook.settingsJson = request.settingsJson;
        ShaderCookResult cooked = ShaderCooker::Cook(cook, &error);
        const auto removeStaging = [&] {
            std::error_code removeError;
            fs::remove(stagingArtifact, removeError);
        };
        if (!cooked.succeeded) {
            removeStaging();
            result.diagnostic = error.empty() ? "shader cache cook failed" : std::move(error);
            if (IsCompilerTimeoutFailure(result.diagnostic))
                result.failureKind = ShaderCacheFailureKind::CompilerTimeout;
            return result;
        }
        if (cancellation && cancellation->IsCancellationRequested()) {
            removeStaging();
            return CancelledResult("shader cache batch cancelled after compiler completion");
        }

        std::string stableKeyError;
        const std::string stableKey = ShaderCooker::BuildCacheKey(source, allowedRoot, request.backends, targetPlatform,
                                                                  request.settingsJson, nullptr, &stableKeyError);
        if (cooked.cacheKey != cacheKey || stableKey != cacheKey) {
            removeStaging();
            if (attempt + 1 < kMaxSourceStabilityAttempts)
                continue;
            result.diagnostic =
                stableKeyError.empty() ? "shader source changed repeatedly while compiling" : std::move(stableKeyError);
            result.artifactPath.clear();
            return result;
        }

        const auto stableSource = LoadShaderAssetFromFile(source.string());
        const auto staged = LoadShaderAssetFromFile(stagingArtifact.string());
        if (!stableSource || !staged || !CookedArtifactMatchesRequest(*staged, *stableSource, request.backends)) {
            removeStaging();
            if (attempt + 1 < kMaxSourceStabilityAttempts)
                continue;
            result.diagnostic = "compiled shader artifact did not match the stable source description";
            result.artifactPath.clear();
            return result;
        }
        if (!SaveCookedShaderAsset(*staged, finalArtifact, &error)) {
            removeStaging();
            result.diagnostic = error.empty() ? "failed to publish shader cache artifact" : std::move(error);
            result.artifactPath.clear();
            return result;
        }
        removeStaging();
        result.succeeded = true;
        result.cacheHit = false;
        result.artifactPath = finalArtifact;
        return result;
    }
    result.diagnostic = "shader cache source stability retry limit exceeded";
    result.artifactPath.clear();
    return result;
}
} // namespace

ShaderCacheService& ShaderCacheService::Get() {
    static ShaderCacheService service;
    return service;
}

void ShaderCacheService::SetResolver(Resolver resolver, BatchResolver batchResolver) {
    std::lock_guard<std::mutex> lock(m_ResolverMutex);
    m_Resolver = std::move(resolver);
    m_BatchResolver = std::move(batchResolver);
    m_CancellableBatchResolver = {};
}

void ShaderCacheService::SetResolverWithCancellation(Resolver resolver, CancellableBatchResolver batchResolver) {
    std::lock_guard<std::mutex> lock(m_ResolverMutex);
    m_Resolver = std::move(resolver);
    m_BatchResolver = {};
    m_CancellableBatchResolver = std::move(batchResolver);
}

void ShaderCacheService::ConfigureFileSystemCache(const std::filesystem::path& cacheRoot, std::string targetPlatform,
                                                  size_t maxConcurrency) {
    const fs::path normalizedRoot = fs::absolute(cacheRoot).lexically_normal();
    if (maxConcurrency == 0) {
        const size_t hardwareThreads = static_cast<size_t>(std::thread::hardware_concurrency());
        // Shader compilation is a development-only subprocess workload. Half the logical CPUs avoids starving the
        // Editor/UI and asset loader while still scaling substantially beyond the old fixed four-worker ceiling.
        maxConcurrency = hardwareThreads == 0 ? 4
                                              : (std::min)(static_cast<size_t>(8),
                                                           (std::max)(static_cast<size_t>(1), hardwareThreads / 2));
    }
    maxConcurrency = (std::max)(static_cast<size_t>(1), maxConcurrency);
    Resolver resolver = [normalizedRoot, targetPlatform](const ShaderCacheRequest& request) {
        return ResolveFileSystemArtifact(request, normalizedRoot, targetPlatform);
    };
    CancellableBatchResolver batch = [normalizedRoot, targetPlatform, maxConcurrency](
                                         const std::vector<ShaderCacheRequest>& requests,
                                         const std::shared_ptr<ShaderCacheBatchCancellation>& callerCancellation) {
        std::vector<ShaderCacheResult> results(requests.size());
        if (requests.empty())
            return results;

        const std::shared_ptr<ShaderCacheBatchCancellation> cancellation =
            callerCancellation ? callerCancellation : std::make_shared<ShaderCacheBatchCancellation>();

        // Resolve virtual Content/Engine paths while still on the caller thread. AssetManager owns mutable lookup
        // tables, whereas all remaining cache-key, file IO, and Slang work is independent per content-addressed path.
        std::vector<ShaderCacheRequest> resolvedRequests = requests;
        for (ShaderCacheRequest& request : resolvedRequests)
            request.sourcePath = ResolveShaderSource(request.sourcePath);

        // Canonical aliases can otherwise make two workers truncate the same content-addressed artifact at once.
        // Cook each unique request once and expand its result back to the caller's stable input order.
        std::vector<ShaderCacheRequest> uniqueRequests;
        std::vector<size_t> requestToUnique;
        std::unordered_map<std::string, size_t> uniqueByKey;
        uniqueRequests.reserve(resolvedRequests.size());
        requestToUnique.reserve(resolvedRequests.size());
        for (const ShaderCacheRequest& request : resolvedRequests) {
            std::string key = request.sourcePath.lexically_normal().generic_string();
            key += request.allowCompile ? "|compile=1" : "|compile=0";
            key += "|settings=" + request.settingsJson;
            for (ShaderBackend backend : request.backends)
                key += "|backend=" + std::to_string(static_cast<uint32_t>(backend));
            const auto [it, inserted] = uniqueByKey.emplace(std::move(key), uniqueRequests.size());
            if (inserted)
                uniqueRequests.push_back(request);
            requestToUnique.push_back(it->second);
        }

        std::vector<ShaderCacheResult> uniqueResults(uniqueRequests.size());
        std::vector<uint8_t> started(uniqueRequests.size(), 0);

        const size_t workerCount = (std::min)(uniqueRequests.size(), maxConcurrency);
        std::atomic_size_t next{0};
        std::vector<std::future<void>> workers;
        workers.reserve(workerCount);
        for (size_t worker = 0; worker < workerCount; ++worker) {
            workers.push_back(std::async(std::launch::async, [&] {
                for (;;) {
                    if (cancellation->IsCancellationRequested())
                        return;
                    const size_t index = next.fetch_add(1);
                    if (index >= uniqueRequests.size())
                        return;
                    if (cancellation->IsCancellationRequested())
                        return;
                    started[index] = 1;
                    try {
                        uniqueResults[index] = ResolveFileSystemArtifact(uniqueRequests[index], normalizedRoot,
                                                                         targetPlatform, cancellation);
                    } catch (const std::exception& exception) {
                        uniqueResults[index].diagnostic =
                            std::string("shader cache worker failed: ") + exception.what();
                    } catch (...) {
                        uniqueResults[index].diagnostic = "shader cache worker failed with an unknown exception";
                    }
                    if (uniqueResults[index].failureKind == ShaderCacheFailureKind::CompilerTimeout)
                        cancellation->RequestCancel();
                }
            }));
        }
        for (auto& worker : workers)
            worker.get();
        for (size_t index = 0; index < uniqueResults.size(); ++index) {
            if (started[index] == 0)
                uniqueResults[index] = CancelledResult();
        }
        for (size_t index = 0; index < results.size(); ++index)
            results[index] = uniqueResults[requestToUnique[index]];
        return results;
    };
    SetResolverWithCancellation(std::move(resolver), std::move(batch));
}

void ShaderCacheService::ClearResolver() {
    std::lock_guard<std::mutex> lock(m_ResolverMutex);
    m_Resolver = {};
    m_BatchResolver = {};
    m_CancellableBatchResolver = {};
}

bool ShaderCacheService::HasResolver() const {
    std::lock_guard<std::mutex> lock(m_ResolverMutex);
    return static_cast<bool>(m_Resolver);
}

ShaderCacheResult ShaderCacheService::EnsureShaderArtifact(const ShaderCacheRequest& request) const {
    Resolver resolver;
    {
        std::lock_guard<std::mutex> lock(m_ResolverMutex);
        resolver = m_Resolver;
    }
    if (!resolver) {
        return {false, false, {}, "shader cache service is not configured"};
    }
    return resolver(request);
}

std::vector<ShaderCacheResult>
ShaderCacheService::EnsureShaderArtifacts(const std::vector<ShaderCacheRequest>& requests,
                                          const std::shared_ptr<ShaderCacheBatchCancellation>& cancellation) const {
    Resolver resolver;
    BatchResolver batchResolver;
    CancellableBatchResolver cancellableBatchResolver;
    {
        std::lock_guard<std::mutex> lock(m_ResolverMutex);
        resolver = m_Resolver;
        batchResolver = m_BatchResolver;
        cancellableBatchResolver = m_CancellableBatchResolver;
    }
    if (cancellableBatchResolver)
        return cancellableBatchResolver(requests, cancellation);
    if (cancellation && cancellation->IsCancellationRequested())
        return std::vector<ShaderCacheResult>(requests.size(), CancelledResult());
    if (batchResolver)
        return batchResolver(requests);

    std::vector<ShaderCacheResult> results;
    results.reserve(requests.size());
    if (!resolver) {
        for (size_t index = 0; index < requests.size(); ++index)
            results.push_back({false, false, {}, "shader cache service is not configured"});
        return results;
    }
    for (const ShaderCacheRequest& request : requests) {
        if (cancellation && cancellation->IsCancellationRequested()) {
            results.resize(requests.size(), CancelledResult());
            break;
        }
        results.push_back(resolver(request));
    }
    return results;
}
