#include "Project/ProjectConfig.h"
#include "Core/Sha256.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace fs = std::filesystem;

void ProjectConfig::SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

bool ProjectConfig::IsWithin(const fs::path& path, const fs::path& parent)
{
    std::error_code ec;
    const fs::path relative = fs::relative(path, parent, ec);
    if (ec || relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

bool ProjectConfig::Open(fs::path projectRoot, bool allowMissing, std::string* error)
{
    if (error) error->clear();
    std::error_code ec;
    m_Root = fs::absolute(std::move(projectRoot), ec).lexically_normal();
    if (ec || m_Root.empty()) {
        SetError(error, "failed to resolve project root");
        return false;
    }
    if (!fs::is_directory(m_Root, ec) || ec) {
        SetError(error, "project root is not a directory: " + m_Root.string());
        return false;
    }

    m_ManifestPath = m_Root / kFileName;
    m_Version = kCurrentVersion;
    m_Name = m_Root.filename().string();
    m_ProjectId.clear();
    if (m_Name.empty()) m_Name = "MyEngine";
    m_StartupScene.clear();
    m_PublishSettings = {};
    m_HasManifest = fs::is_regular_file(m_ManifestPath, ec) && !ec;
    if (!m_HasManifest) {
        if (allowMissing) return true;
        SetError(error, "project manifest not found: " + m_ManifestPath.string());
        return false;
    }

    try {
        std::ifstream input(m_ManifestPath);
        if (!input) {
            SetError(error, "failed to open project manifest: " + m_ManifestPath.string());
            return false;
        }
        nlohmann::json json;
        input >> json;
        if (!json.is_object()) {
            SetError(error, "project manifest root must be an object");
            return false;
        }
        m_Version = json.value("version", 0);
        m_Name = json.value("name", std::string{});
        m_StartupScene = json.value("startupScene", std::string{});
        m_ProjectId = json.value("projectId", std::string{});
        if (const auto publish = json.find("publish");
            publish != json.end() && publish->is_object()) {
            m_PublishSettings.outputDirectory =
                publish->value("outputDirectory", std::string{"Builds"});
            m_PublishSettings.target =
                publish->value("target", std::string{PublishTargets::kDefaultTargetId});
        }
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to parse project manifest: " + std::string(exception.what()));
        return false;
    }

    if (m_Version != kCurrentVersion) {
        SetError(error, "unsupported project version: " + std::to_string(m_Version));
        return false;
    }
    if (m_Name.empty()) {
        SetError(error, "project name must not be empty");
        return false;
    }
    if (m_ProjectId.empty()) {
        Sha256 hash; const std::string identity=m_Name+"|"+m_StartupScene;
        hash.Update(identity.data(),identity.size()); m_ProjectId=Sha256::ToHex(hash.Final());
    }
    if (m_PublishSettings.outputDirectory.empty()) {
        SetError(error, "publish outputDirectory must not be empty");
        return false;
    }
    if (!PublishTargets::IsSupported(m_PublishSettings.target)) {
        SetError(error, "unsupported publish target: " + m_PublishSettings.target);
        return false;
    }
    if (!m_StartupScene.empty()) {
        fs::path ignored;
        if (!ResolveScenePath(m_StartupScene, ignored, false, error)) return false;
    }
    return true;
}

bool ProjectConfig::Save(std::string* error)
{
    if (error) error->clear();
    if (m_Root.empty() || m_ManifestPath.empty()) {
        SetError(error, "project config has not been opened");
        return false;
    }
    if (m_Name.empty()) {
        SetError(error, "project name must not be empty");
        return false;
    }
    if (m_PublishSettings.outputDirectory.empty()) {
        SetError(error, "publish outputDirectory must not be empty");
        return false;
    }
    if (!PublishTargets::IsSupported(m_PublishSettings.target)) {
        SetError(error, "unsupported publish target: " + m_PublishSettings.target);
        return false;
    }
    if (!m_StartupScene.empty()) {
        fs::path ignored;
        if (!ResolveScenePath(m_StartupScene, ignored, true, error)) return false;
    }

    try {
        nlohmann::json json;
        json["version"] = kCurrentVersion;
        json["name"] = m_Name;
        json["projectId"] = m_ProjectId;
        json["startupScene"] = m_StartupScene;
        json["publish"] = {
            {"outputDirectory", m_PublishSettings.outputDirectory},
            {"target", m_PublishSettings.target},
        };
        std::ofstream output(m_ManifestPath);
        if (!output) {
            SetError(error, "failed to write project manifest: " + m_ManifestPath.string());
            return false;
        }
        output << json.dump(2) << '\n';
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to save project manifest: " + std::string(exception.what()));
        return false;
    }
    m_HasManifest = true;
    m_Version = kCurrentVersion;
    return true;
}

bool ProjectConfig::SetStartupScene(const fs::path& scenePath, std::string* error)
{
    if (error) error->clear();
    if (m_Root.empty()) {
        SetError(error, "project config has not been opened");
        return false;
    }

    std::error_code ec;
    fs::path absoluteScene = scenePath.is_absolute()
        ? scenePath.lexically_normal()
        : fs::absolute(m_Root / scenePath, ec).lexically_normal();
    if (ec || !fs::is_regular_file(absoluteScene, ec) || ec) {
        SetError(error, "startup scene does not exist: " + absoluteScene.string());
        return false;
    }

    const fs::path contentRoot = (m_Root / "Content").lexically_normal();
    if (!IsWithin(absoluteScene, contentRoot)) {
        SetError(error, "startup scene must be inside the project Content directory");
        return false;
    }

    const fs::path relative = fs::relative(absoluteScene, m_Root, ec);
    if (ec || relative.empty()) {
        SetError(error, "failed to make startup scene project-relative");
        return false;
    }
    m_StartupScene = relative.generic_string();
    return true;
}

bool ProjectConfig::ResolveStartupScene(fs::path& resolved, std::string* error) const
{
    if (error) error->clear();
    if (m_StartupScene.empty()) {
        SetError(error, "project startupScene is not configured");
        return false;
    }
    return ResolveScenePath(m_StartupScene, resolved, true, error);
}

bool ProjectConfig::ResolveScenePath(const std::string& projectRelativePath,
                                     fs::path& resolved, bool requireExists,
                                     std::string* error) const
{
    if (error) error->clear();
    if (m_Root.empty()) {
        SetError(error, "project config has not been opened");
        return false;
    }
    if (projectRelativePath.empty()) {
        SetError(error, "scene path must not be empty");
        return false;
    }

    const fs::path input(projectRelativePath);
    if (input.is_absolute() || input.has_root_name() || input.has_root_directory()) {
        SetError(error, "scene path must be project-relative");
        return false;
    }

    resolved = (m_Root / input).lexically_normal();
    const fs::path contentRoot = (m_Root / "Content").lexically_normal();
    if (!IsWithin(resolved, contentRoot)) {
        SetError(error, "scene path must stay inside the project Content directory");
        return false;
    }
    if (requireExists) {
        std::error_code ec;
        if (!fs::is_regular_file(resolved, ec) || ec) {
            SetError(error, "scene file does not exist: " + resolved.string());
            return false;
        }
    }
    return true;
}
