#include "Renderer/ShaderCooker.h"

#include "Assets/AssetDatabase.h"
#include "Core/Sha256.h"
#include "Project/RuntimeCompatibility.h"
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#include "Renderer/ShaderCompilerSlang.h"
#include "Renderer/ShaderGraphCompiler.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error)
        *error = std::move(message);
}

bool IsWithin(const fs::path& path, const fs::path& parent) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, parent, ec);
    return !ec && !relative.empty() && !relative.is_absolute() && relative.begin() != relative.end() &&
           *relative.begin() != "..";
}

bool ValidateShaderIncludes(const fs::path& source, const fs::path& allowedRoot,
                            std::unordered_set<std::string>& visited, std::string* error) {
    std::error_code ec;
    const fs::path canonicalSource = fs::weakly_canonical(source, ec);
    if (ec) {
        SetError(error, "shader source is missing: " + source.string());
        return false;
    }
    const fs::path canonicalRoot = fs::weakly_canonical(allowedRoot, ec);
    if (ec || !IsWithin(canonicalSource, canonicalRoot)) {
        SetError(error, "shader source escapes Content root: " + source.string());
        return false;
    }
    if (!visited.insert(canonicalSource.generic_string()).second)
        return true;

    std::ifstream input(canonicalSource);
    if (!input) {
        SetError(error, "shader source is missing: " + source.string());
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const size_t directive = line.find("#include");
        if (directive == std::string::npos)
            continue;
        const size_t begin = line.find_first_of("\"<", directive + 8);
        if (begin == std::string::npos) {
            SetError(error, "invalid shader include: " + source.string());
            return false;
        }
        const char close = line[begin] == '<' ? '>' : '\"';
        const size_t end = line.find(close, begin + 1);
        if (end == std::string::npos) {
            SetError(error, "invalid shader include: " + source.string());
            return false;
        }
        const fs::path includePath(line.substr(begin + 1, end - begin - 1));
        if (includePath.is_absolute()) {
            SetError(error, "absolute shader include is forbidden");
            return false;
        }
        for (const auto& part : includePath) {
            if (part == "..") {
                SetError(error, "parent shader include is forbidden");
                return false;
            }
        }
        const fs::path resolved = fs::weakly_canonical(canonicalSource.parent_path() / includePath, ec);
        if (ec || !IsWithin(resolved, canonicalRoot)) {
            SetError(error, "shader include escapes Content root: " + includePath.string());
            return false;
        }
        if (!ValidateShaderIncludes(resolved, canonicalRoot, visited, error))
            return false;
    }
    return true;
}

bool CompileShaderStageForBackend(const fs::path& hlsl, const ShaderStageSource& sourceStage, ShaderStage stage,
                                  ShaderBackend backend, const std::vector<std::string>& defines,
                                  std::vector<uint8_t>& outBlob, std::string* error) {
    if (backend == ShaderBackend::Metal || backend == ShaderBackend::Vulkan) {
        return ShaderCompilerSlang::CompileStageFromFile(hlsl, sourceStage.entry, stage, backend, outBlob, defines,
                                                         error);
    }

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (backend == ShaderBackend::D3D11 || backend == ShaderBackend::D3D12) {
        std::vector<unsigned char> fallback;
        const size_t stageIndex = static_cast<size_t>(stage);
        const char* profiles11[] = {"vs_5_0", "ps_5_0", "cs_5_0"};
        const char* profiles12[] = {"vs_5_1", "ps_5_1", "cs_5_1"};
        const bool ok = backend == ShaderBackend::D3D12
                            ? ShaderCompilerD3D12::CompileStageFromFile(hlsl.string(), sourceStage.entry,
                                                                        profiles12[stageIndex], fallback, defines)
                            : ShaderCompilerD3D11::CompileStageFromFile(hlsl.string(), sourceStage.entry,
                                                                        profiles11[stageIndex], fallback, defines);
        if (ok) {
            outBlob.assign(fallback.begin(), fallback.end());
            return true;
        }
    }
#endif

    if (error && error->empty()) {
        *error = std::string("shader compile failed for backend ") + ShaderCooker::BackendName(backend) + ": " +
                 hlsl.string();
    }
    return false;
}
} // namespace

namespace ShaderCooker {
const char* BackendName(ShaderBackend backend) {
    switch (backend) {
    case ShaderBackend::D3D11:
        return "d3d11";
    case ShaderBackend::D3D12:
        return "d3d12";
    case ShaderBackend::Metal:
        return "metal";
    case ShaderBackend::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

std::vector<ShaderBackend> BackendsForTargetPlatform(const std::string& targetPlatform) {
    if (targetPlatform == "macos-arm64")
        return {ShaderBackend::Metal};
#if defined(MYENGINE_ENABLE_VULKAN)
    return {ShaderBackend::D3D11, ShaderBackend::D3D12, ShaderBackend::Vulkan};
#else
    return {ShaderBackend::D3D11, ShaderBackend::D3D12};
#endif
}

bool CollectDependencies(const fs::path& source, const fs::path& allowedRoot, std::vector<std::string>& outDependencies,
                         std::string* error) {
    auto description = LoadShaderAssetFromFile(source.string());
    if (!description || description->IsCooked()) {
        SetError(error, "invalid shader description: " + source.string());
        return false;
    }

    std::unordered_set<std::string> dependencySet;
    if (description->IsGraph()) {
        std::vector<ShaderGraphDiagnostic> diagnostics;
        if (!ShaderGraphCompiler::Validate(description->GetGraph(), description->GetProperties(), diagnostics)) {
            const auto found = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.severity == ShaderGraphDiagnostic::Severity::Error;
            });
            SetError(error, found == diagnostics.end() ? "invalid shader graph" : found->message);
            return false;
        }
        for (const auto& property : description->GetProperties()) {
            if (property.type != ShaderPropertyType::Texture2D || property.defaultTexture.empty() ||
                property.defaultTexture.rfind("__builtin__/", 0) == 0)
                continue;
            fs::path texturePath(property.defaultTexture);
            if (!texturePath.is_absolute()) {
                const std::string generic = texturePath.generic_string();
                texturePath = generic.rfind("Content/", 0) == 0 ? allowedRoot.parent_path() / texturePath
                                                                : source.parent_path() / texturePath;
            }
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(texturePath, ec);
            if (!ec && IsWithin(canonical, allowedRoot))
                dependencySet.insert(canonical.generic_string());
        }
        outDependencies.assign(dependencySet.begin(), dependencySet.end());
        std::sort(outDependencies.begin(), outDependencies.end());
        return true;
    }
    for (size_t passIndex = 0; passIndex < static_cast<size_t>(ShaderPass::Count); ++passIndex) {
        const auto pass = static_cast<ShaderPass>(passIndex);
        if (!description->HasPass(pass))
            continue;
        for (size_t stageIndex = 0; stageIndex < kShaderStageCount; ++stageIndex) {
            const auto stage = static_cast<ShaderStage>(stageIndex);
            const auto& sourceStage = description->GetPassStage(pass, stage);
            if (sourceStage.source.empty())
                continue;
            const fs::path hlsl = description->ResolveSource(pass, stage);
            std::unordered_set<std::string> visited;
            if (!ValidateShaderIncludes(hlsl, allowedRoot, visited, error))
                return false;
            dependencySet.insert(visited.begin(), visited.end());
        }
    }

    outDependencies.assign(dependencySet.begin(), dependencySet.end());
    std::sort(outDependencies.begin(), outDependencies.end());
    return true;
}

std::string BuildCacheKey(const fs::path& source, const fs::path& allowedRoot,
                          const std::vector<ShaderBackend>& backends, const std::string& targetPlatform,
                          const std::string& settingsJson, std::vector<std::string>* outDependencies,
                          std::string* error) {
    auto description = LoadShaderAssetFromFile(source.string());
    if (!description || description->IsCooked()) {
        SetError(error, "invalid shader description: " + source.string());
        return {};
    }

    std::vector<std::string> dependencies;
    if (!CollectDependencies(source, allowedRoot, dependencies, error))
        return {};
    if (outDependencies)
        *outDependencies = dependencies;

    const bool usesSlang = std::find(backends.begin(), backends.end(), ShaderBackend::Metal) != backends.end() ||
                           std::find(backends.begin(), backends.end(), ShaderBackend::Vulkan) != backends.end();

    Sha256 cacheKey;
    const std::string graphContract =
        description->IsGraph()
            ? ShaderGraphCompiler::BuildCanonicalKey(description->GetGraph(), description->GetProperties(),
                                                     description->GetShadingModel(), description->GetSurfaceType())
            : std::string{};
    const std::string cookerContract = std::string(RuntimeCompatibility::kBuildId) + "|shader-cooker-v4|" +
                                       (usesSlang ? ShaderCompilerSlang::GetVersionString() : "fxc") + "|" +
                                       std::to_string(description->GetSourceHash()) + "|" + targetPlatform + "|" +
                                       settingsJson + "|" + graphContract;
    cacheKey.Update(cookerContract.data(), cookerContract.size());
    for (ShaderBackend backend : backends) {
        const std::string backendText = std::to_string(static_cast<int>(backend));
        cacheKey.Update(backendText.data(), backendText.size());
    }
    for (const std::string& dependency : dependencies) {
        std::string hashError;
        const std::string hash = Sha256::HashFile(dependency, &hashError);
        if (!hashError.empty()) {
            SetError(error, hashError);
            return {};
        }
        cacheKey.Update(dependency.data(), dependency.size());
        cacheKey.Update(hash.data(), hash.size());
    }
    return Sha256::ToHex(cacheKey.Final());
}

ShaderCookResult Cook(const ShaderCookRequest& request, std::string* error) {
    ShaderCookResult result;
    result.artifactPath = request.artifactPath;
    const std::vector<ShaderBackend> backends =
        request.backends.empty() ? BackendsForTargetPlatform(request.targetPlatform) : request.backends;

    result.cacheKey = BuildCacheKey(request.sourcePath, request.allowedRoot, backends, request.targetPlatform,
                                    request.settingsJson, &result.dependencies, error);
    if (result.cacheKey.empty()) {
        result.diagnostics.push_back({"error", error ? *error : "shader cache key failed"});
        return result;
    }

    auto description = LoadShaderAssetFromFile(request.sourcePath.string());
    if (!description || description->IsCooked()) {
        SetError(error, "invalid shader description: " + request.sourcePath.string());
        result.diagnostics.push_back({"error", error ? *error : "invalid shader"});
        return result;
    }

    if (request.artifactPath.stem().string() == result.cacheKey) {
        if (auto cached = LoadShaderAssetFromFile(request.artifactPath.string());
            cached && cached->IsCooked() && cached->GetStageMask() == description->GetStageMask() &&
            cached->GetPassMask() == description->GetPassMask() &&
            cached->GetSourceHash() == description->GetSourceHash()) {
            result.succeeded = true;
            result.cacheHit = true;
            return result;
        }
    }

    std::error_code ec;
    fs::create_directories(request.artifactPath.parent_path(), ec);
    if (ec) {
        SetError(error, "failed to create shader artifact directory: " + ec.message());
        result.diagnostics.push_back({"error", error ? *error : "directory create failed"});
        return result;
    }
    constexpr size_t passCount = static_cast<size_t>(ShaderPass::Count);
    std::array<std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, passCount>, kShaderBackendCount> blobs{};
    for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
        const auto pass = static_cast<ShaderPass>(passIndex);
        if (!description->HasPass(pass))
            continue;

        fs::path generatedPath;
        std::array<ShaderStageSource, kShaderStageCount> generatedStages{};
        if (description->IsGraph()) {
            ShaderGraphCompileRequest graphRequest;
            graphRequest.graph = &description->GetGraph();
            graphRequest.properties = &description->GetProperties();
            graphRequest.shadingModel = description->GetShadingModel();
            graphRequest.surfaceType = description->GetSurfaceType();
            graphRequest.pass = pass;
            const ShaderGraphCompileResult generated = ShaderGraphCompiler::Compile(graphRequest);
            if (!generated.succeeded) {
                const auto found = std::find_if(
                    generated.diagnostics.begin(), generated.diagnostics.end(), [](const auto& diagnostic) {
                        return diagnostic.severity == ShaderGraphDiagnostic::Severity::Error;
                    });
                SetError(error,
                         found == generated.diagnostics.end() ? "shader graph generation failed" : found->message);
                result.diagnostics.push_back({"error", error ? *error : "shader graph generation failed"});
                return result;
            }
            generatedPath = request.artifactPath.parent_path() /
                            (request.artifactPath.stem().string() + "." + ShaderPassName(pass) + ".generated.hlsl");
            {
                std::ofstream generatedFile(generatedPath, std::ios::binary | std::ios::trunc);
                generatedFile.write(generated.hlsl.data(), static_cast<std::streamsize>(generated.hlsl.size()));
                if (!generatedFile) {
                    SetError(error, "failed writing generated shader source: " + generatedPath.string());
                    result.diagnostics.push_back({"error", error ? *error : "generated source write failed"});
                    return result;
                }
            }
            generatedStages[static_cast<size_t>(ShaderStage::Vertex)] = {generatedPath.filename().string(), "VSMain"};
            generatedStages[static_cast<size_t>(ShaderStage::Pixel)] = {generatedPath.filename().string(), "PSMain"};
        }

        for (size_t stageIndex = 0; stageIndex < kShaderStageCount; ++stageIndex) {
            const auto stage = static_cast<ShaderStage>(stageIndex);
            const uint32_t stageBit = 1u << stageIndex;
            if ((description->GetStageMask() & stageBit) == 0)
                continue;
            const ShaderStageSource& sourceStage =
                description->IsGraph() ? generatedStages[stageIndex] : description->GetPassStage(pass, stage);
            if (sourceStage.source.empty())
                continue;
            const fs::path hlsl = description->IsGraph() ? generatedPath : description->ResolveSource(pass, stage);
            for (ShaderBackend backend : backends) {
                if (!CompileShaderStageForBackend(hlsl, sourceStage, stage, backend, description->GetDefines(),
                                                  blobs[static_cast<size_t>(backend)][passIndex][stageIndex], error)) {
                    if (!generatedPath.empty())
                        fs::remove(generatedPath, ec);
                    if (error && error->empty())
                        *error = "shader cook failed: " + request.sourcePath.string();
                    result.diagnostics.push_back({"error", error ? *error : "shader cook failed"});
                    return result;
                }
            }
        }
        if (!generatedPath.empty())
            fs::remove(generatedPath, ec);
    }

    ShaderAsset cooked(request.artifactPath.string());
    cooked.SetCookedPasses(description->GetPassMask(), description->GetSourceHash(), description->GetSourceMode(),
                           description->GetDomain(), description->GetShadingModel(), description->GetSurfaceType(),
                           description->GetProperties(), std::move(blobs));
    result.succeeded = SaveCookedShaderAsset(cooked, request.artifactPath, error);
    if (!result.succeeded) {
        result.diagnostics.push_back({"error", error ? *error : "failed writing shader artifact"});
    }
    return result;
}
} // namespace ShaderCooker
