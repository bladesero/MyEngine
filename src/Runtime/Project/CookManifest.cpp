#include "Project/CookManifest.h"
#include "Core/TransactionalFileWriter.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/JsonMigrationRegistry.h"
#include "Core/Sha256.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool SafeContentPath(const std::string& value) {
    const fs::path path(value);
    if (value.empty() || path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) return false;
    auto part = path.begin();
    if (part == path.end() || *part != "Content") return false;
    for (; part != path.end(); ++part) {
        if (*part == ".." || *part == ".") return false;
    }
    return true;
}

std::vector<std::string> ExpectedRequiredBackends(const std::string& target)
{
    if (target == PublishTargets::kMacOSArm64.id) return {"metal"};
#if defined(MYENGINE_ENABLE_VULKAN)
    return {"d3d11", "d3d12", "vulkan"};
#else
    return {"d3d11", "d3d12"};
#endif
}
}

bool CookManifest::Validate(std::string* error) const {
    if (error) error->clear();
    if (version != kCurrentVersion) {
        SetError(error, "unsupported Cook manifest version: " + std::to_string(version));
        return false;
    }
    if (project.empty()) {
        SetError(error, "Cook manifest project must not be empty");
        return false;
    }
    Sha256::Digest digest{};
    if(projectId.empty() || engineVersion.empty() || buildId.empty() || configuration.empty() ||
       gitCommit.empty() || compiler.empty() || shaderToolVersion.empty() ||
       contentSchemaVersion!=RuntimeCompatibility::kContentSchemaVersion ||
       archiveFormatVersion!=RuntimeCompatibility::kArchiveFormatVersion ||
       hashAlgorithm!="sha256" || !Sha256::FromHex(archiveHash,digest) ||
       !Sha256::FromHex(runtimeDependenciesHash,digest)) {
        SetError(error,"Cook manifest compatibility contract is invalid"); return false;
    }
    if (!PublishTargets::IsSupported(target)) {
        SetError(error, "unsupported Cook target: " + target);
        return false;
    }
    const auto expectedBackends = ExpectedRequiredBackends(target);
    if(requiredBackends != expectedBackends) {
        SetError(error,"Cook manifest requiredBackends do not match target: " + target); return false;
    }
    if (!SafeContentPath(startupScene)) {
        SetError(error, "Cook manifest startupScene must be a safe Content path");
        return false;
    }
    if (archive != ContentArchive::kFileName) {
        SetError(error, "Cook manifest archive must be Content.pak");
        return false;
    }
    if (files.empty()) {
        SetError(error, "Cook manifest file list must not be empty");
        return false;
    }

    uint64_t total = 0;
    bool hasStartupScene = false;
    std::unordered_set<std::string> paths;
    for (const auto& file : files) {
        Sha256::Digest fileDigest{};
        if (!SafeContentPath(file.path) || !paths.insert(file.path).second ||
            !Sha256::FromHex(file.hash,fileDigest)) {
            SetError(error, "Cook manifest contains an unsafe or duplicate path: " + file.path);
            return false;
        }
        if (file.size > (std::numeric_limits<uint64_t>::max)() - total) {
            SetError(error, "Cook manifest content size overflow");
            return false;
        }
        total += file.size;
        hasStartupScene = hasStartupScene || file.path == startupScene;
    }
    if (total != contentBytes) {
        SetError(error, "Cook manifest contentBytes does not match the file list");
        return false;
    }
    if (!hasStartupScene) {
        SetError(error, "Cook manifest file list does not contain startupScene");
        return false;
    }
    return true;
}

bool CookManifest::Save(const fs::path& path, std::string* error) const {
    if (error) error->clear();
    if (!Validate(error)) return false;
    try {
        nlohmann::json fileList = nlohmann::json::array();
        for (const auto& file : files) {
            fileList.push_back({
                {"path", file.path}, {"size", file.size}, {"hash", file.hash},
            });
        }
        const nlohmann::json json = {
            {"version", version},
            {"project", project},
            {"projectId",projectId},{"engineVersion",engineVersion},{"buildId",buildId},
            {"gitCommit",gitCommit},{"compiler",compiler},{"shaderToolVersion",shaderToolVersion},
            {"contentSchemaVersion",contentSchemaVersion},{"archiveFormatVersion",archiveFormatVersion},
            {"hashAlgorithm",hashAlgorithm},{"configuration",configuration},
            {"requiredBackends",requiredBackends},{"runtimeDependenciesHash",runtimeDependenciesHash},
            {"target", target},
            {"startupScene", startupScene},
            {"archive", archive},
            {"archiveHash", archiveHash},
            {"contentBytes", contentBytes},
            {"files", std::move(fileList)},
        };
        TransactionalWriteOptions options;
        options.validator=[](const fs::path& candidate,std::string* validationError){CookManifest ignored;return CookManifest::Load(candidate,ignored,validationError);};
        return TransactionalFileWriter::WriteText(path,json.dump(2)+"\n",options,error);
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to save Cook manifest: " + std::string(exception.what()));
        return false;
    }
}

bool CookManifest::Load(const fs::path& path, CookManifest& manifest,
                        std::string* error) {
    if (error) error->clear();
    try {
        std::ifstream input(path);
        if (!input) {
            SetError(error, "Cook manifest not found: " + path.string());
            return false;
        }
        nlohmann::json json;
        input >> json;
        JsonMigrationRegistry migrations("Cook manifest", kCurrentVersion);
        if (!migrations.Migrate(json, error)) return false;
        CookManifest loaded;
        loaded.version = json.value("version", 0);
        loaded.project = json.value("project", std::string{});
        loaded.projectId=json.value("projectId",std::string{});
        loaded.engineVersion=json.value("engineVersion",std::string{});
        loaded.buildId=json.value("buildId",std::string{});
        loaded.gitCommit=json.value("gitCommit",std::string{"unknown"});
        loaded.compiler=json.value("compiler",std::string{"unknown"});
        loaded.shaderToolVersion=json.value("shaderToolVersion",std::string{"unknown"});
        loaded.contentSchemaVersion=json.value("contentSchemaVersion",0);
        loaded.archiveFormatVersion=json.value("archiveFormatVersion",0);
        loaded.hashAlgorithm=json.value("hashAlgorithm",std::string{});
        loaded.configuration=json.value("configuration",std::string{});
        loaded.requiredBackends=json.value("requiredBackends",std::vector<std::string>{});
        loaded.runtimeDependenciesHash=json.value("runtimeDependenciesHash",std::string{});
        loaded.target = json.value("target", std::string{});
        loaded.startupScene = json.value("startupScene", std::string{});
        loaded.archive = json.value("archive", std::string{});
        loaded.archiveHash = json.value("archiveHash", std::string{});
        loaded.contentBytes = json.value("contentBytes", uint64_t{0});
        const auto files = json.find("files");
        if (files == json.end() || !files->is_array()) {
            SetError(error, "Cook manifest files must be an array");
            return false;
        }
        for (const auto& item : *files) {
            if (!item.is_object()) {
                SetError(error, "Cook manifest file entry must be an object");
                return false;
            }
            loaded.files.push_back({
                item.value("path", std::string{}),
                item.value("size", uint64_t{0}),
                item.value("hash", std::string{}),
            });
        }
        if (!loaded.Validate(error)) return false;
        manifest = std::move(loaded);
        return true;
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to parse Cook manifest: " + std::string(exception.what()));
        return false;
    }
}
