#include "Editor/ProjectValidator.h"

#include "Assets/ScriptAsset.h"
#include "Assets/AssetManager.h"
#include "Project/ContentPathPolicy.h"
#include "Project/ProjectConfig.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace {
class ScopedProjectRoot {
public:
    explicit ScopedProjectRoot(const fs::path& root) : m_Previous(AssetManager::Get().GetProjectRoot()) {
        AssetManager::Get().SetProjectRoot(root);
    }
    ~ScopedProjectRoot() { AssetManager::Get().SetProjectRoot(m_Previous); }

private:
    fs::path m_Previous;
};

void Add(ProjectValidationReport& report, ProjectValidationSeverity severity, ProjectValidationCode code,
         std::string path, std::string referrer, std::string message) {
    report.issues.push_back({severity, code, std::move(path), std::move(referrer), std::move(message)});
}
} // namespace

size_t ProjectValidationReport::ErrorCount() const {
    return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [](const ProjectValidationIssue& issue) {
        return issue.severity == ProjectValidationSeverity::Error;
    }));
}

size_t ProjectValidationReport::WarningCount() const {
    return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [](const ProjectValidationIssue& issue) {
        return issue.severity == ProjectValidationSeverity::Warning;
    }));
}

std::string ProjectValidationReport::Summary() const {
    std::string result = Passed() ? "project validation passed" : "project validation failed";
    result += " (" + std::to_string(ErrorCount()) + " errors, " + std::to_string(WarningCount()) + " warnings, " +
              std::to_string(scannedFiles) + " files)";
    if (!Passed()) {
        const auto first = std::find_if(issues.begin(), issues.end(), [](const ProjectValidationIssue& issue) {
            return issue.severity == ProjectValidationSeverity::Error;
        });
        if (first != issues.end())
            result += ": " + first->message;
    }
    return result;
}

bool ProjectValidator::Validate(const ProjectConfig& project, ProjectValidationReport& report,
                                const ProjectValidationOptions& options) {
    report = {};
    const fs::path root = project.GetRoot();
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec) || ec) {
        Add(report, ProjectValidationSeverity::Error, ProjectValidationCode::InvalidProject, root.generic_string(), {},
            "project root does not exist or is not a directory");
        return false;
    }

    fs::path startupScene;
    std::string startupError;
    const bool startupResolved = project.ResolveStartupScene(startupScene, &startupError);
    if (!startupResolved || !fs::is_regular_file(startupScene, ec) || ec) {
        if (startupResolved)
            startupError = "startup scene does not exist inside the project: " + startupScene.generic_string();
        Add(report, ProjectValidationSeverity::Error, ProjectValidationCode::InvalidStartupScene,
            project.GetStartupScene(), project.GetManifestPath().generic_string(), startupError);
    }

    CookDependencyGraph::Validate(root, report.preflight);
    report.contentBytes = report.preflight.totalContentBytes;
    for (const PublishIssue& issue : report.preflight.errors) {
        Add(report, ProjectValidationSeverity::Error, ProjectValidationCode::CookDependency, issue.path, issue.referrer,
            issue.message);
    }

    std::vector<ContentFileInfo> files;
    uint64_t enumeratedBytes = 0;
    std::string enumerationError;
    if (!ContentPathPolicy::Enumerate(root / "Content", files, enumeratedBytes, &enumerationError)) {
        Add(report, ProjectValidationSeverity::Error, ProjectValidationCode::InvalidProject, "Content", {},
            enumerationError);
    } else {
        ScopedProjectRoot projectRoot(root);
        report.scannedFiles = static_cast<uint32_t>(files.size());
        report.contentBytes = enumeratedBytes;
        for (const ContentFileInfo& file : files) {
            const std::string logical = (fs::path("Content") / file.relative).generic_string();
            if (options.oversizedAssetWarningBytes > 0 && file.size > options.oversizedAssetWarningBytes) {
                Add(report, ProjectValidationSeverity::Warning, ProjectValidationCode::OversizedAsset, logical, {},
                    "asset is larger than the configured warning threshold (" + std::to_string(file.size) + " bytes)");
            }
            std::string extension = file.absolute.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (extension != ".as" || file.relative.generic_string().rfind("Editor/", 0) == 0)
                continue;
            const std::shared_ptr<ScriptAsset> script = LoadScriptAssetFromFile(file.absolute.string());
            if (!script || !script->GetLastError().empty()) {
                Add(report, ProjectValidationSeverity::Error, ProjectValidationCode::ScriptCompile, logical, {},
                    script ? script->GetLastError() : "script loader returned no asset");
            }
        }
    }

    std::sort(report.issues.begin(), report.issues.end(),
              [](const ProjectValidationIssue& left, const ProjectValidationIssue& right) {
                  if (left.severity != right.severity)
                      return static_cast<int>(left.severity) > static_cast<int>(right.severity);
                  if (left.path != right.path)
                      return left.path < right.path;
                  if (left.referrer != right.referrer)
                      return left.referrer < right.referrer;
                  return left.message < right.message;
              });
    return report.Passed();
}
