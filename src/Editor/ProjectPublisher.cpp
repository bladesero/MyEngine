#include "Editor/ProjectPublisher.h"

#include "Project/CookManifest.h"
#include "Project/PublishTargets.h"
#include "Project/ProjectConfig.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImporter.h"
#include "Assets/ShaderAsset.h"
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#include "Renderer/ShaderCompilerSlang.h"
#include "Renderer/ShaderCooker.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"
#include "Project/ContentPathPolicy.h"
#include "Core/Sha256.h"
#include "Core/TransactionalFileWriter.h"
#include "Core/BuildInfo.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string SafeName(std::string name) {
    for (char& value : name) {
        const unsigned char c = static_cast<unsigned char>(value);
        if (!std::isalnum(c) && value != '-' && value != '_') value = '_';
    }
    return name.empty() ? "Project" : name;
}

bool CopyRequired(const fs::path& source, const fs::path& destination,
                  std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(source, ec) || ec) {
        SetError(error, "required runtime file is missing: " + source.string());
        return false;
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError(error, "failed to copy runtime file: " + source.string() + ": " + ec.message());
        return false;
    }
    return true;
}

std::vector<ShaderBackend> ShaderBackendsForTarget(const std::string& target)
{
    if (target == PublishTargets::kMacOSArm64.id) return {ShaderBackend::Metal};
#if defined(MYENGINE_ENABLE_VULKAN)
    return {ShaderBackend::D3D11, ShaderBackend::D3D12, ShaderBackend::Vulkan};
#else
    return {ShaderBackend::D3D11, ShaderBackend::D3D12};
#endif
}

bool WriteSupplyChainDocuments(const fs::path& staging, std::string* error)
{
    struct Package { const char* name; const char* version; const char* license; };
    static const Package packages[] = {
        {"SDL3","3.2.14","Zlib"},{"nlohmann-json","3.11.3","MIT"},
        {"stb","project-pinned","MIT OR Unlicense"},{"RmlUi","6.2","MIT"},
        {"tinyobjloader","project-pinned","MIT"},{"JoltPhysics","5.5.0","MIT"},
        {"AngelScript","2.38.0","Zlib"},{"Dear ImGui","1.91.3-docking","MIT"},
        {"ImGuizmo","project-pinned","MIT"},{"miniaudio","project-pinned","MIT-0 OR Unlicense"},
        {"cgltf","project-pinned","MIT"},{"Lua","project-pinned","MIT"}
    };
    nlohmann::json packageArray=nlohmann::json::array(),noticeArray=nlohmann::json::array();
    for(const auto& package:packages){
        std::string packageId=package.name;for(char& value:packageId)if(!std::isalnum(static_cast<unsigned char>(value)))value='-';
        packageArray.push_back({{"SPDXID",std::string("SPDXRef-Package-")+packageId},
            {"name",package.name},{"versionInfo",package.version},
            {"downloadLocation","NOASSERTION"},{"licenseConcluded",package.license},
            {"licenseDeclared",package.license},{"filesAnalyzed",false}});
        noticeArray.push_back({{"name",package.name},{"version",package.version},
                               {"license",package.license}});
    }
    const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc,&now);
#else
    gmtime_r(&now,&utc);
#endif
    std::ostringstream created;created<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
    const nlohmann::json sbom={{"spdxVersion","SPDX-2.3"},{"dataLicense","CC0-1.0"},
        {"SPDXID","SPDXRef-DOCUMENT"},{"name",std::string("MyEngine-")+std::string(BuildInfo::BuildId)},
        {"documentNamespace",std::string("https://myengine.local/spdx/")+std::string(BuildInfo::BuildId)},
        {"creationInfo",{{"created",created.str()},{"creators",nlohmann::json::array({"Tool: MyEnginePublisher"})},
                         {"comment",std::string("commit=")+std::string(BuildInfo::GitCommit)}}},
        {"packages",packageArray}};
    const nlohmann::json notices={{"engineVersion",std::string(BuildInfo::EngineVersion)},
        {"buildId",std::string(BuildInfo::BuildId)},{"gitCommit",std::string(BuildInfo::GitCommit)},
        {"packages",noticeArray}};
    return TransactionalFileWriter::WriteText(staging/"SBOM.spdx.json",sbom.dump(2)+"\n",{},error)&&
           TransactionalFileWriter::WriteText(staging/"ThirdPartyNotices.json",notices.dump(2)+"\n",{},error);
}

std::vector<std::string> RequiredBackendNamesForTarget(const std::string& target)
{
    if (target == PublishTargets::kMacOSArm64.id) return {"metal"};
#if defined(MYENGINE_ENABLE_VULKAN)
    return {"d3d11", "d3d12", "vulkan"};
#else
    return {"d3d11", "d3d12"};
#endif
}

const char* ShaderBackendName(ShaderBackend backend)
{
    switch (backend) {
    case ShaderBackend::D3D11: return "d3d11";
    case ShaderBackend::D3D12: return "d3d12";
    case ShaderBackend::Metal: return "metal";
    case ShaderBackend::Vulkan: return "vulkan";
    }
    return "unknown";
}

bool HostCanPublishTarget(const std::string& target)
{
#if defined(MYENGINE_PLATFORM_WINDOWS)
    return target == PublishTargets::kWindowsX64.id;
#elif defined(MYENGINE_PLATFORM_MACOS)
    return target == PublishTargets::kMacOSArm64.id;
#else
    (void)target;
    return false;
#endif
}

bool IsWithin(const fs::path& path, const fs::path& parent) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, parent, ec);
    return !ec && !relative.empty() && !relative.is_absolute() &&
        relative.begin() != relative.end() && *relative.begin() != "..";
}

std::string AbsoluteKey(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.lexically_normal().generic_string();
}

std::string ToProjectContentReference(const fs::path& projectRoot,
                                      const std::string& sourcePath) {
    fs::path source(sourcePath);
    if (source.is_relative() && source.generic_string().rfind("Content/", 0) == 0) {
        return source.generic_string();
    }
    if (source.is_relative()) source = projectRoot / source;
    std::error_code ec;
    source = fs::absolute(source, ec).lexically_normal();
    if (ec || !IsWithin(source, projectRoot / "Content")) return {};
    return (fs::path("Content") / fs::relative(source, projectRoot / "Content", ec)).generic_string();
}

struct PublishArtifactMap {
    std::unordered_map<std::string, std::string> rewrites;
    std::unordered_map<std::string, std::string> uuidRewrites;
    std::vector<std::pair<fs::path, fs::path>> artifacts;
    std::unordered_set<std::string> replacedContent;
};

std::optional<std::string> ExtractProjectLibraryArtifactUuid(
    const fs::path& projectRoot,
    const fs::path& path) {
    std::error_code ec;
    fs::path absolute = path.is_absolute() || path.has_root_name()
        ? fs::absolute(path, ec)
        : fs::absolute(path, ec);
    if (ec) absolute = path;
    absolute = absolute.lexically_normal();
    fs::path libraryRoot = fs::absolute(projectRoot / "Library" / "windows-x64", ec);
    if (ec) libraryRoot = projectRoot / "Library" / "windows-x64";
    libraryRoot = libraryRoot.lexically_normal();
    const fs::path relative = fs::relative(absolute, libraryRoot, ec);
    if (ec || relative.empty() || relative.is_absolute()) return std::nullopt;
    auto part = relative.begin();
    if (part == relative.end() || *part == "..") return std::nullopt;
    const std::string uuid = part->generic_string();
    ++part;
    if (part == relative.end()) return std::nullopt;
    return uuid;
}

std::unordered_map<std::string, std::string> BuildArtifactReferenceMap(
    const fs::path& projectRoot) {
    std::unordered_map<std::string, std::string> result;
    AssetDatabase database;
    std::string error;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error)) return result;
    for (const AssetRecord& record : database.GetAll()) {
        if (record.artifactPath.empty() || record.sourcePath.empty()) continue;
        const std::string sourceReference =
            ToProjectContentReference(projectRoot, record.sourcePath);
        if (sourceReference.empty()) continue;
        result[AbsoluteKey(record.artifactPath)] = sourceReference;
    }
    return result;
}

std::vector<std::string> CollectGltfExternalContentReferences(
    const fs::path& projectRoot,
    const fs::path& sourcePath) {
    std::vector<std::string> references;
    if (sourcePath.extension() != ".gltf") return references;
    try {
        std::ifstream input(sourcePath);
        if (!input) return references;
        nlohmann::json gltf;
        input >> gltf;
        auto collectUri = [&](const nlohmann::json& node) {
            const std::string uri = node.value("uri", std::string{});
            if (uri.empty() || uri.find(':') != std::string::npos) return;
            const fs::path dependency = (sourcePath.parent_path() / fs::path(uri)).lexically_normal();
            const std::string reference = ToProjectContentReference(projectRoot, dependency.generic_string());
            if (!reference.empty()) references.push_back(reference);
        };
        for (const nlohmann::json& buffer : gltf.value("buffers", nlohmann::json::array())) {
            collectUri(buffer);
        }
        for (const nlohmann::json& image : gltf.value("images", nlohmann::json::array())) {
            collectUri(image);
        }
    } catch (...) {
    }
    return references;
}

PublishArtifactMap BuildPublishArtifactMap(
    const fs::path& projectRoot,
    const std::unordered_set<std::string>& referencedContent) {
    PublishArtifactMap result;
    AssetDatabase database;
    std::string error;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", &error)) return result;
    for (const AssetRecord& record : database.GetAll()) {
        if (record.type != "model" || record.artifactPath.empty() ||
            fs::path(record.artifactPath).extension() != ".modelbin") {
            continue;
        }
        const std::string sourceReference =
            ToProjectContentReference(projectRoot, record.sourcePath);
        if (sourceReference.empty() || !referencedContent.count(sourceReference)) continue;

        const fs::path artifactPath = fs::path(record.artifactPath).lexically_normal();
        std::error_code ec;
        if (!fs::is_regular_file(artifactPath, ec) || ec) continue;
        const fs::path publishedRelative = fs::path("Content") / "Library" /
            "windows-x64" / record.uuid / artifactPath.filename();
        const std::string publishedReference = publishedRelative.generic_string();

        result.rewrites[sourceReference] = publishedReference;
        result.rewrites[AbsoluteKey(record.sourcePath)] = publishedReference;
        result.rewrites[AbsoluteKey(record.artifactPath)] = publishedReference;
        result.uuidRewrites[record.uuid] = publishedReference;

        const fs::path destinationArtifact =
            fs::path("Library") / "windows-x64" / record.uuid / artifactPath.filename();
        result.artifacts.push_back({artifactPath, destinationArtifact});
        result.replacedContent.insert(sourceReference);
        for (const std::string& dependency :
             CollectGltfExternalContentReferences(projectRoot, fs::path(record.sourcePath))) {
            result.replacedContent.insert(dependency);
        }
    }
    return result;
}

bool RefreshReferencedModelArtifacts(
    const fs::path& projectRoot,
    const std::unordered_set<std::string>& referencedContent,
    std::string* error) {
    AssetDatabase database;
    if (!database.Open(projectRoot / ".myengine" / "AssetDatabase.json", error)) return false;
    std::unordered_map<std::string, AssetRecord> recordsBySource;
    for (const AssetRecord& record : database.GetAll()) {
        if (record.sourcePath.empty()) continue;
        recordsBySource[AbsoluteKey(record.sourcePath)] = record;
    }
    const std::unique_ptr<IAssetImporter> importer = CreateGltfModelAssetImporter();
    bool changed = false;
    for (const std::string& reference : referencedContent) {
        fs::path source = fs::path(reference);
        std::string extension = source.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension != ".gltf" && extension != ".glb") continue;
        if (source.is_relative()) source = projectRoot / source;
        const auto existing = recordsBySource.find(AbsoluteKey(source));
        if (existing == recordsBySource.end()) continue;
        AssetRecord record = existing->second;
        if (record.artifactPath.empty() || record.uuid.empty()) continue;
        if (record.importerVersion == importer->GetVersion() &&
            fs::is_regular_file(record.artifactPath)) {
            continue;
        }
        if (!fs::is_regular_file(source.string() + ".meta")) continue;
        ImportRequest request{source, record.artifactPath, record.uuid,
                              record.settingsJson.empty() ? "{}" : record.settingsJson,
                              "windows-x64"};
        ImportResult result = importer->Import(request);
        record.importer = importer->GetName();
        record.importerVersion = importer->GetVersion();
        record.diagnostics = std::move(result.diagnostics);
        record.type = std::move(result.type);
        record.state = result.succeeded ? AssetImportState::Ready : AssetImportState::Failed;
        if (!result.succeeded) {
            SetError(error, "failed to import model artifact for publish: " + source.string());
            return false;
        }
        std::string hashError;
        record.artifactHash = Sha256::HashFile(record.artifactPath, &hashError);
        if (!hashError.empty()) {
            SetError(error, hashError);
            return false;
        }
        if (!database.Upsert(record, error)) return false;
        changed = true;
    }
    return !changed || database.Save(error);
}

bool RewriteImportedArtifactReferences(nlohmann::json& node,
                                       const fs::path& projectRoot,
                                       const std::unordered_map<std::string, std::string>& map,
                                       const std::unordered_map<std::string, std::string>& uuidMap) {
    bool changed = false;
    if (node.is_string()) {
        const std::string value = node.get<std::string>();
        const size_t fragmentPosition = value.find('#');
        const std::string base = value.substr(0, fragmentPosition);
        auto it = map.find(base);
        if (it == map.end()) {
            const fs::path basePath(base);
            if (basePath.is_absolute() || basePath.has_root_name()) {
                it = map.find(AbsoluteKey(basePath));
            }
        }
        if (it != map.end()) {
            node = it->second + (fragmentPosition == std::string::npos
                ? std::string{} : value.substr(fragmentPosition));
            return true;
        }
        const fs::path basePath(base);
        if (basePath.is_absolute() || basePath.has_root_name()) {
            if (const std::optional<std::string> uuid =
                    ExtractProjectLibraryArtifactUuid(projectRoot, basePath)) {
                const auto uuidIt = uuidMap.find(*uuid);
                if (uuidIt != uuidMap.end()) {
                    node = uuidIt->second + (fragmentPosition == std::string::npos
                        ? std::string{} : value.substr(fragmentPosition));
                    return true;
                }
            }
        }
        return false;
    }
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            changed = RewriteImportedArtifactReferences(
                it.value(), projectRoot, map, uuidMap) || changed;
        }
    } else if (node.is_array()) {
        for (auto& item : node) {
            changed = RewriteImportedArtifactReferences(
                item, projectRoot, map, uuidMap) || changed;
        }
    }
    return changed;
}

bool CopyJsonWithImportedArtifactRewrite(
    const fs::path& source, const fs::path& destination,
    const fs::path& projectRoot,
    const std::unordered_map<std::string, std::string>& artifactReferences,
    const std::unordered_map<std::string, std::string>& artifactUuidReferences,
    std::string* error) {
    if (artifactReferences.empty() && artifactUuidReferences.empty()) {
        std::error_code ec;
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) SetError(error, "failed to stage content: " + source.string());
        return !ec;
    }
    try {
        std::ifstream input(source);
        nlohmann::json json;
        input >> json;
        const bool changed = RewriteImportedArtifactReferences(
            json, projectRoot, artifactReferences, artifactUuidReferences);
        if (!changed) {
            std::error_code ec;
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
            if (ec) SetError(error, "failed to stage content: " + source.string());
            return !ec;
        }
        std::ofstream output(destination);
        output << json.dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        SetError(error, "failed to rewrite imported artifact references in " +
            source.string() + ": " + exception.what());
        return false;
    }
}

void Cleanup(const fs::path& path) {
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

fs::path FindEngineContentRoot() {
    fs::path cursor = fs::current_path();
    while (!cursor.empty()) {
        const fs::path candidate = cursor / "EngineContent";
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && !ec) return candidate;
        const fs::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return {};
}

bool ValidateShaderIncludes(const fs::path& source, const fs::path& allowedRoot,
                            std::unordered_set<std::string>& visited,
                            std::string* error) {
    std::error_code ec;
    const fs::path canonicalSource = fs::weakly_canonical(source, ec);
    const fs::path canonicalRoot = fs::weakly_canonical(allowedRoot, ec);
    if (ec || !IsWithin(canonicalSource, canonicalRoot)) {
        SetError(error, "shader source escapes Content root: " + source.string()); return false;
    }
    if (!visited.insert(canonicalSource.generic_string()).second) return true;
    std::ifstream input(canonicalSource);
    if (!input) { SetError(error, "shader source is missing: " + source.string()); return false; }
    std::string line;
    while (std::getline(input, line)) {
        const size_t directive = line.find("#include");
        if (directive == std::string::npos) continue;
        const size_t begin = line.find_first_of("\"<", directive + 8);
        if (begin == std::string::npos) { SetError(error, "invalid shader include: " + source.string()); return false; }
        const char close = line[begin] == '<' ? '>' : '\"';
        const size_t end = line.find(close, begin + 1);
        if (end == std::string::npos) { SetError(error, "invalid shader include: " + source.string()); return false; }
        const fs::path includePath(line.substr(begin + 1, end - begin - 1));
        if (includePath.is_absolute()) { SetError(error, "absolute shader include is forbidden"); return false; }
        for (const auto& part : includePath) if (part == "..") {
            SetError(error, "parent shader include is forbidden"); return false;
        }
        const fs::path resolved = fs::weakly_canonical(canonicalSource.parent_path() / includePath, ec);
        if (ec || !IsWithin(resolved, canonicalRoot)) {
            SetError(error, "shader include escapes Content root: " + includePath.string()); return false;
        }
        if (!ValidateShaderIncludes(resolved, canonicalRoot, visited, error)) return false;
    }
    return true;
}

bool CompileShaderStageForBackend(const fs::path& hlsl,
                                  const ShaderStageSource& sourceStage,
                                  ShaderStage stage,
                                  ShaderBackend backend,
                                  const std::vector<std::string>& defines,
                                  std::vector<uint8_t>& outBlob,
                                  std::string* error) {
    if (backend == ShaderBackend::Metal || backend == ShaderBackend::Vulkan) {
        return ShaderCompilerSlang::CompileStageFromFile(
            hlsl, sourceStage.entry, stage, backend, outBlob, defines, error);
    }

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (backend == ShaderBackend::D3D11 || backend == ShaderBackend::D3D12) {
        std::vector<unsigned char> fallback;
        const size_t stageIndex = static_cast<size_t>(stage);
        const char* profiles11[] = {"vs_5_0", "ps_5_0", "cs_5_0"};
        const char* profiles12[] = {"vs_5_1", "ps_5_1", "cs_5_1"};
        const bool ok = backend == ShaderBackend::D3D12
            ? ShaderCompilerD3D12::CompileStageFromFile(
                hlsl.string(), sourceStage.entry, profiles12[stageIndex],
                fallback, defines)
            : ShaderCompilerD3D11::CompileStageFromFile(
                hlsl.string(), sourceStage.entry, profiles11[stageIndex],
                fallback, defines);
        if (ok) {
            outBlob.assign(fallback.begin(), fallback.end());
            return true;
        }
    }
#endif

    if (error && error->empty()) {
        *error = std::string("shader compile failed for backend ") +
            ShaderBackendName(backend) + ": " + hlsl.string();
    }
    return false;
}

bool CompileCookedShader(const fs::path& source, const fs::path& destination,
                         const fs::path& allowedRoot,
                         const std::vector<ShaderBackend>& backends,
                         std::string* error) {
    ShaderCookRequest request;
    request.sourcePath = source;
    request.artifactPath = destination;
    request.allowedRoot = allowedRoot;
    request.backends = backends;
    request.targetPlatform = "windows-x64";
    const ShaderCookResult result = ShaderCooker::Cook(request, error);
    return result.succeeded;
}

bool CookTree(const fs::path& sourceRoot, const fs::path& destinationRoot,
              const std::vector<ShaderBackend>& shaderBackends,
              const fs::path& projectRoot,
              const std::unordered_map<std::string, std::string>* artifactReferences,
              const std::unordered_map<std::string, std::string>* artifactUuidReferences,
              const std::unordered_set<std::string>* allowedContent,
              std::string* error) {
    std::error_code ec; std::vector<ContentFileInfo> files; uint64_t total=0;
    if(!ContentPathPolicy::Enumerate(sourceRoot,files,total,error)) return false;
    std::vector<std::future<std::pair<bool,std::string>>> shaderTasks;
    for (const auto& file : files) {
        const fs::path relative=file.relative;
        const std::string contentReference =
            (fs::path("Content") / relative).generic_string();
        if (allowedContent && !allowedContent->count(contentReference)) continue;
        std::string extension = file.absolute.extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string relativeText = relative.generic_string();
        std::transform(relativeText.begin(), relativeText.end(), relativeText.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".lua" && relativeText.rfind("editor/scripts/", 0) == 0) continue;
        if (extension == ".hlsl" || extension == ".hlsli") continue;
        const fs::path destination = destinationRoot / relative;
        if (extension == ".shader") {
            const fs::path shaderSource=file.absolute;
            shaderTasks.emplace_back(std::async(std::launch::async,
                [shaderSource,destination,sourceRoot,shaderBackends] {
                    std::string taskError;
                    const bool result=CompileCookedShader(
                        shaderSource,destination,sourceRoot,shaderBackends,&taskError);
                    return std::make_pair(result,std::move(taskError));
                }));
        } else {
            fs::create_directories(destination.parent_path(), ec);
            if (ec) { SetError(error, "failed to create staged content directory: " + destination.parent_path().string()); return false; }
            if ((extension == ".json" || extension == ".mat") && artifactReferences) {
                if (!CopyJsonWithImportedArtifactRewrite(file.absolute, destination,
                                                         projectRoot,
                                                         *artifactReferences,
                                                         artifactUuidReferences
                                                             ? *artifactUuidReferences
                                                             : std::unordered_map<std::string, std::string>{},
                                                         error)) {
                    return false;
                }
            } else {
                fs::copy_file(file.absolute, destination, fs::copy_options::overwrite_existing, ec);
                if (ec) { SetError(error, "failed to stage content: " + file.absolute.string()); return false; }
            }
        }
    }
    for(auto& task:shaderTasks) {
        auto [succeeded,taskError]=task.get();
        if(!succeeded){SetError(error,std::move(taskError));return false;}
    }
    return true;
}

bool CookEngineRuntimeContent(const fs::path& sourceRoot,
                              const fs::path& destinationRoot,
                              const std::vector<ShaderBackend>& shaderBackends,
                              std::string* error) {
    std::error_code ec;
    std::vector<ContentFileInfo> files;
    uint64_t total = 0;
    if (!ContentPathPolicy::Enumerate(sourceRoot, files, total, error)) return false;
    std::vector<std::future<std::pair<bool, std::string>>> shaderTasks;
    for (const auto& file : files) {
        const fs::path relative = file.relative;
        const std::string relativeText = relative.generic_string();
        if (relativeText.rfind("Editor/", 0) == 0) continue;
        if (relativeText.rfind("Shaders/", 0) != 0 &&
            relativeText.rfind("Scripts/", 0) != 0) {
            continue;
        }
        std::string extension = file.absolute.extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == ".hlsl" || extension == ".hlsli") continue;
        const fs::path destination = destinationRoot / relative;
        if (extension == ".shader") {
            const fs::path shaderSource = file.absolute;
            shaderTasks.emplace_back(std::async(std::launch::async,
                [shaderSource, destination, sourceRoot, shaderBackends] {
                    std::string taskError;
                    const bool result = CompileCookedShader(
                        shaderSource, destination, sourceRoot, shaderBackends, &taskError);
                    return std::make_pair(result, std::move(taskError));
                }));
            continue;
        }
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create staged engine content directory: " +
                destination.parent_path().string());
            return false;
        }
        fs::copy_file(file.absolute, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetError(error, "failed to stage engine content: " + file.absolute.string());
            return false;
        }
    }
    for (auto& task : shaderTasks) {
        auto [succeeded, taskError] = task.get();
        if (!succeeded) {
            SetError(error, std::move(taskError));
            return false;
        }
    }
    return true;
}

bool CopyPublishArtifacts(
    const fs::path& destinationRoot,
    const std::vector<std::pair<fs::path, fs::path>>& artifacts,
    std::string* error) {
    std::error_code ec;
    auto copyFile = [&](const fs::path& source, const fs::path& destination) {
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create staged model cache directory: " +
                destination.parent_path().string());
            return false;
        }
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetError(error, "failed to stage model cache artifact: " +
                source.string());
            return false;
        }
        return true;
    };
    for (const auto& [sourceArtifact, relativeArtifact] : artifacts) {
        if (!fs::is_regular_file(sourceArtifact, ec) || ec) {
            SetError(error, "model cache artifact is missing: " +
                sourceArtifact.string());
            return false;
        }
        if (!copyFile(sourceArtifact, destinationRoot / relativeArtifact)) return false;

        const fs::path sourcePayloadDirectory =
            sourceArtifact.parent_path() / (sourceArtifact.filename().string() + "_textures");
        if (!fs::is_directory(sourcePayloadDirectory, ec) || ec) {
            ec.clear();
            continue;
        }
        const fs::path relativePayloadDirectory =
            relativeArtifact.parent_path() / sourcePayloadDirectory.filename();
        for (fs::recursive_directory_iterator it(sourcePayloadDirectory, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (extension != ".texturebin") continue;
            const fs::path relative = fs::relative(it->path(), sourcePayloadDirectory, ec);
            if (ec || relative.empty()) {
                SetError(error, "failed to compute staged model cache path: " +
                    it->path().string());
                return false;
            }
            if (!copyFile(it->path(), destinationRoot / relativePayloadDirectory / relative)) return false;
        }
        if (ec) {
            SetError(error, "failed to enumerate model cache payload directory: " +
                sourcePayloadDirectory.string());
            return false;
        }
    }
    return true;
}
}

bool ProjectPublisher::Publish(const ProjectConfig& project,
                               const fs::path& engineBinaryDirectory,
                               const fs::path& engineContentDirectory,
                               PublishReport& report,
                               std::string* error) {
    if (error) error->clear();
    report = {};
    fs::path startup;
    if (!project.ResolveStartupScene(startup, error)) return false;
    if(!CookDependencyGraph::Validate(project.GetRoot(),report.preflight)) {
        SetError(error,report.preflight.Summary()); return false;
    }

    const auto& settings = project.GetPublishSettings();
    if (!PublishTargets::IsSupported(settings.target)) {
        SetError(error, "unsupported publish target: " + settings.target);
        return false;
    }
    if (!HostCanPublishTarget(settings.target)) {
        SetError(error, "publish target '" + settings.target +
            "' is not supported by this host build; current host target is '" +
            std::string(PublishTargets::kDefaultTargetId) + "'");
        return false;
    }
    const std::vector<ShaderBackend> shaderBackends =
        ShaderBackendsForTarget(settings.target);
    std::error_code ec;
    if (!fs::is_directory(project.GetRoot() / "Content", ec) || ec) {
        SetError(error, "project Content directory is missing");
        return false;
    }
    if (fs::exists(project.GetRoot() / "Content" / "Engine", ec) && !ec) {
        SetError(error, "project Content must not override reserved Content/Engine");
        return false;
    }
    fs::path engineContent = engineContentDirectory.empty()
        ? fs::path{}
        : fs::absolute(engineContentDirectory).lexically_normal();
    if (!engineContent.empty() && (!fs::is_directory(engineContent, ec) || ec)) {
        ec.clear();
        engineContent.clear();
    }
    if (engineContent.empty()) engineContent = FindEngineContentRoot();
    if (engineContent.empty()) {
        SetError(error, "EngineContent root is missing"); return false;
    }
    fs::path outputBase(settings.outputDirectory);
    if (!outputBase.is_absolute()) outputBase = project.GetRoot() / outputBase;
    outputBase = outputBase.lexically_normal();
    const fs::path finalDirectory = outputBase /
        (SafeName(project.GetName()) + "-" + SafeName(settings.target));
    const fs::path staging = finalDirectory.string() + ".staging";
    const fs::path backup = finalDirectory.string() + ".backup";
    if (finalDirectory == project.GetRoot() ||
        IsWithin(finalDirectory, project.GetRoot() / "Content")) {
        SetError(error, "publish directory must not replace or be inside the project Content root");
        return false;
    }

    // Recover a previous interrupted restore before starting another publish.
    const bool finalExists = fs::exists(finalDirectory, ec) && !ec;
    ec.clear();
    if (fs::exists(backup, ec) && !ec) {
        if (!finalExists) {
            fs::rename(backup, finalDirectory, ec);
            if (ec) {
                SetError(error, "failed to restore interrupted publish backup: " + ec.message());
                return false;
            }
        } else {
            Cleanup(backup);
        }
    }

#ifdef _WIN32
    const char* required[] = {"MyEnginePlayer.exe", "runtime.dll", "SDL3.dll"};
#elif defined(__APPLE__)
    const char* required[] = {"MyEnginePlayer", "libruntime.dylib", "libSDL3.dylib", "libSDL3.0.dylib"};
#else
    const char* required[] = {"MyEnginePlayer", "libruntime.so", "libSDL3.so"};
#endif
    // Preflight every required input before touching an existing successful package.
    for (const char* name : required) {
        if (!fs::is_regular_file(engineBinaryDirectory / name, ec) || ec) {
            SetError(error, "required runtime file is missing: " +
                     (engineBinaryDirectory / name).string());
            return false;
        }
    }
    if (!fs::is_regular_file(project.GetManifestPath(), ec) || ec) {
        SetError(error, "project manifest is missing: " + project.GetManifestPath().string());
        return false;
    }

    Cleanup(staging);
    fs::create_directories(staging, ec);
    if (ec) {
        SetError(error, "failed to create publish staging directory: " + ec.message());
        return false;
    }

    RuntimeDependencyManifest runtimeDependencies;
#ifdef _WIN32
    if(!WindowsRuntimeDependencyCollector::Collect(engineBinaryDirectory,staging,runtimeDependencies,error) ||
       !runtimeDependencies.Save(staging/RuntimeDependencyManifest::kFileName,error)) {
        Cleanup(staging); return false;
    }
#else
    if(!HostRuntimeDependencyCollector::Collect(
           engineBinaryDirectory, staging, runtimeDependencies,
           std::vector<std::string>(std::begin(required), std::end(required)), error) ||
       !runtimeDependencies.Save(staging/RuntimeDependencyManifest::kFileName,error)) {
        Cleanup(staging); return false;
    }
#endif
    const std::string runtimeDependenciesHash=Sha256::HashFile(
        staging/RuntimeDependencyManifest::kFileName,error);
    if(runtimeDependenciesHash.empty()){Cleanup(staging);return false;}
    if(!WriteSupplyChainDocuments(staging,error)){Cleanup(staging);return false;}
    if (!CopyRequired(project.GetManifestPath(), staging / ProjectConfig::kFileName, error)) {
        Cleanup(staging);
        return false;
    }

    std::vector<CookedContentEntry> entries;
    const fs::path archive = staging / ContentArchive::kFileName;
    const fs::path cookedContent = staging / ".cooked-content";
    std::unordered_set<std::string> referencedContent;
    for (const std::string& asset : report.preflight.visitedAssets) {
        fs::path path(asset);
        if (path.is_absolute() || path.has_root_name()) continue;
        const std::string generic = path.lexically_normal().generic_string();
        if (generic.rfind("Content/", 0) == 0 &&
            generic.rfind("Content/Engine/", 0) != 0) {
            referencedContent.insert(generic);
        }
    }
    if (!RefreshReferencedModelArtifacts(project.GetRoot(), referencedContent, error)) {
        Cleanup(staging);
        return false;
    }
    const PublishArtifactMap publishArtifacts =
        BuildPublishArtifactMap(project.GetRoot(), referencedContent);
    for (const std::string& replaced : publishArtifacts.replacedContent) {
        referencedContent.erase(replaced);
    }
    auto artifactReferences = BuildArtifactReferenceMap(project.GetRoot());
    for (const auto& [from, to] : publishArtifacts.rewrites) {
        artifactReferences[from] = to;
    }
    fs::create_directories(cookedContent / "Engine", ec);
    if (ec || !CookEngineRuntimeContent(engineContent, cookedContent / "Engine", shaderBackends, error) ||
        !CookTree(project.GetRoot() / "Content", cookedContent, shaderBackends,
                  project.GetRoot(), &artifactReferences,
                  &publishArtifacts.uuidRewrites, &referencedContent, error) ||
        !CopyPublishArtifacts(cookedContent, publishArtifacts.artifacts, error) ||
        !ContentArchive::Create(cookedContent, archive, &entries, error)) {
        Cleanup(staging);
        return false;
    }
    Cleanup(cookedContent);
    uint64_t contentBytes = 0;
    for (const auto& entry : entries) {
        contentBytes += entry.size;
    }
    std::string hashError;
    const std::string archiveHash = ContentArchive::HashFile(archive, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        Cleanup(staging);
        return false;
    }
    CookManifest manifest;
    manifest.project = project.GetName();
    manifest.projectId=project.GetProjectId();
    manifest.engineVersion=RuntimeCompatibility::kEngineVersion;
    manifest.buildId=RuntimeCompatibility::kBuildId;
    manifest.contentSchemaVersion=RuntimeCompatibility::kContentSchemaVersion;
    manifest.archiveFormatVersion=RuntimeCompatibility::kArchiveFormatVersion;
    manifest.configuration=RuntimeCompatibility::kConfiguration;
    manifest.requiredBackends=RequiredBackendNamesForTarget(settings.target);
    manifest.runtimeDependenciesHash=runtimeDependenciesHash;
    manifest.target = settings.target;
    manifest.startupScene = project.GetStartupScene();
    manifest.archiveHash = archiveHash;
    manifest.contentBytes = contentBytes;
    manifest.files = entries;
    if (!manifest.Save(staging / CookManifest::kFileName, error)) {
        Cleanup(staging);
        return false;
    }
    CookManifest verifiedManifest;
    RuntimeDependencyManifest verifiedDependencies;
    const fs::path verifyRoot=staging/".verify";
    if(!CookManifest::Load(staging/CookManifest::kFileName,verifiedManifest,error)) {
        Cleanup(staging); return false;
    }
    std::string verificationHashError;
    const std::string verificationArchiveHash =
        ContentArchive::HashFile(archive, &verificationHashError);
    if(!verificationHashError.empty() ||
       verifiedManifest.archiveHash != verificationArchiveHash) {
        SetError(error, verificationHashError.empty()
            ? "staged archive SHA-256 mismatch" : verificationHashError);
        Cleanup(staging); return false;
    }
    if(!RuntimeDependencyManifest::Load(
           staging/RuntimeDependencyManifest::kFileName,verifiedDependencies,error) ||
       !verifiedDependencies.ValidateFiles(staging,error) ||
       !ContentArchive::Extract(archive,verifyRoot,error)) {
        Cleanup(staging); return false;
    }
    Cleanup(verifyRoot);

    bool hasBackup = false;
    if (fs::exists(finalDirectory, ec) && !ec) {
        fs::rename(finalDirectory, backup, ec);
        if (ec) {
            SetError(error, "failed to back up the previous published project: " + ec.message());
            Cleanup(staging);
            return false;
        }
        hasBackup = true;
    }
    fs::rename(staging, finalDirectory, ec);
    if (ec) {
        const std::string installError = ec.message();
        if (hasBackup) {
            std::error_code restoreError;
            fs::rename(backup, finalDirectory, restoreError);
            if (restoreError) {
                SetError(error, "failed to install published project: " + installError +
                         "; failed to restore previous package: " + restoreError.message());
                Cleanup(staging);
                return false;
            }
        }
        SetError(error, "failed to install published project: " + installError);
        Cleanup(staging);
        return false;
    }
    Cleanup(backup);
    report.outputDirectory = finalDirectory;
    report.contentArchive = finalDirectory / ContentArchive::kFileName;
    report.contentBytes = contentBytes;
    report.cookedFiles = std::move(entries);
    return true;
}
