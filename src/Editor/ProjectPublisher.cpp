#include "Editor/ProjectPublisher.h"

#include "Project/CookManifest.h"
#include "Project/PublishTargets.h"
#include "Project/ProjectConfig.h"
#include "Assets/ShaderAsset.h"
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"
#include "Project/ContentPathPolicy.h"
#include "Core/Sha256.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <future>
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

bool IsWithin(const fs::path& path, const fs::path& parent) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, parent, ec);
    return !ec && !relative.empty() && !relative.is_absolute() &&
        relative.begin() != relative.end() && *relative.begin() != "..";
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

bool CompileCookedShader(const fs::path& source, const fs::path& destination,
                         const fs::path& allowedRoot,
                         std::string* error) {
    auto description = LoadShaderAssetFromFile(source.string());
    if (!description || description->IsCooked()) {
        SetError(error, "invalid shader description: " + source.string()); return false;
    }
    std::unordered_set<std::string> dependencySet;
    for (size_t stageIndex = 0; stageIndex < 3; ++stageIndex) {
        if ((description->GetStageMask() & (1u << stageIndex)) == 0) continue;
        const fs::path hlsl = description->ResolveSource(static_cast<ShaderStage>(stageIndex));
        std::unordered_set<std::string> visited;
        if (!ValidateShaderIncludes(hlsl, allowedRoot, visited, error)) return false;
        dependencySet.insert(visited.begin(), visited.end());
    }
    std::vector<std::string> dependencies(dependencySet.begin(),dependencySet.end());
    std::sort(dependencies.begin(),dependencies.end());
    Sha256 cacheKey;
    const std::string cookerContract=std::string(RuntimeCompatibility::kBuildId)+
        "|shader-cooker-v1|d3d11-sm5.0|d3d12-sm5.1|"+
        std::to_string(description->GetSourceHash());
    cacheKey.Update(cookerContract.data(),cookerContract.size());
    for(const auto& dependency:dependencies) {
        std::string hashError;
        const std::string hash=Sha256::HashFile(dependency,&hashError);
        if(!hashError.empty()){SetError(error,hashError);return false;}
        cacheKey.Update(dependency.data(),dependency.size());
        cacheKey.Update(hash.data(),hash.size());
    }
    const fs::path cacheDirectory=fs::temp_directory_path()/"MyEngine/DerivedDataCache/Shaders";
    const fs::path cacheFile=cacheDirectory/(Sha256::ToHex(cacheKey.Final())+".shader");
    if(auto cached=LoadShaderAssetFromFile(cacheFile.string());
       cached&&cached->IsCooked()&&cached->GetStageMask()==description->GetStageMask()) {
        std::error_code ec;
        fs::create_directories(destination.parent_path(),ec);
        fs::copy_file(cacheFile,destination,fs::copy_options::overwrite_existing,ec);
        if(!ec)return true;
    }

    std::array<std::array<std::vector<uint8_t>, 3>, 2> blobs{};
    for (size_t stageIndex = 0; stageIndex < 3; ++stageIndex) {
        if ((description->GetStageMask() & (1u << stageIndex)) == 0) continue;
        const auto stage = static_cast<ShaderStage>(stageIndex);
        const auto& sourceStage = description->GetStage(stage);
        const fs::path hlsl = description->ResolveSource(stage);
        const char* profiles11[] = {"vs_5_0", "ps_5_0", "cs_5_0"};
        const char* profiles12[] = {"vs_5_1", "ps_5_1", "cs_5_1"};
        if (!ShaderCompilerD3D11::CompileStageFromFile(hlsl.string(), sourceStage.entry,
                profiles11[stageIndex], blobs[0][stageIndex], description->GetDefines()) ||
            !ShaderCompilerD3D12::CompileStageFromFile(hlsl.string(), sourceStage.entry,
                profiles12[stageIndex], blobs[1][stageIndex], description->GetDefines())) {
            SetError(error, "shader cook failed: " + source.string()); return false;
        }
    }
    ShaderAsset cooked(destination.string());
    cooked.SetCooked(description->GetStageMask(), description->GetSourceHash(), std::move(blobs));
    fs::create_directories(destination.parent_path());
    if(!SaveCookedShaderAsset(cooked, destination, error))return false;
    std::error_code ec;
    fs::create_directories(cacheDirectory,ec);
    if(!ec)fs::copy_file(destination,cacheFile,fs::copy_options::overwrite_existing,ec);
    return true;
}

bool CookTree(const fs::path& sourceRoot, const fs::path& destinationRoot,
              std::string* error) {
    std::error_code ec; std::vector<ContentFileInfo> files; uint64_t total=0;
    if(!ContentPathPolicy::Enumerate(sourceRoot,files,total,error)) return false;
    std::vector<std::future<std::pair<bool,std::string>>> shaderTasks;
    for (const auto& file : files) {
        const fs::path relative=file.relative;
        std::string extension = file.absolute.extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == ".hlsl" || extension == ".hlsli") continue;
        const fs::path destination = destinationRoot / relative;
        if (extension == ".shader") {
            const fs::path shaderSource=file.absolute;
            shaderTasks.emplace_back(std::async(std::launch::async,
                [shaderSource,destination,sourceRoot] {
                    std::string taskError;
                    const bool result=CompileCookedShader(
                        shaderSource,destination,sourceRoot,&taskError);
                    return std::make_pair(result,std::move(taskError));
                }));
        } else {
            fs::create_directories(destination.parent_path(), ec);
            fs::copy_file(file.absolute, destination, fs::copy_options::overwrite_existing, ec);
            if (ec) { SetError(error, "failed to stage content: " + file.absolute.string()); return false; }
        }
    }
    for(auto& task:shaderTasks) {
        auto [succeeded,taskError]=task.get();
        if(!succeeded){SetError(error,std::move(taskError));return false;}
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
#ifndef _WIN32
    SetError(error, std::string(PublishTargets::kDefaultTargetId) +
        " publishing is only available on Windows");
    return false;
#endif
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
    const char* required[] = {"MyEnginePlayer", "libruntime.dylib", "libSDL3.dylib"};
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
    if(!WindowsRuntimeDependencyCollector::Collect(engineBinaryDirectory,staging,runtimeDependencies,error) ||
       !runtimeDependencies.Save(staging/RuntimeDependencyManifest::kFileName,error)) {
        Cleanup(staging); return false;
    }
    const std::string runtimeDependenciesHash=Sha256::HashFile(
        staging/RuntimeDependencyManifest::kFileName,error);
    if(runtimeDependenciesHash.empty()){Cleanup(staging);return false;}
    if (!CopyRequired(project.GetManifestPath(), staging / ProjectConfig::kFileName, error)) {
        Cleanup(staging);
        return false;
    }

    std::vector<CookedContentEntry> entries;
    const fs::path archive = staging / ContentArchive::kFileName;
    const fs::path cookedContent = staging / ".cooked-content";
    fs::create_directories(cookedContent / "Engine", ec);
    if (ec || !CookTree(engineContent, cookedContent / "Engine", error) ||
        !CookTree(project.GetRoot() / "Content", cookedContent, error) ||
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
    manifest.requiredBackends={"d3d11","d3d12"};
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
