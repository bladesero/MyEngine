#pragma once

#include "Editor/CookDependencyGraph.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class ProjectConfig;

enum class ProjectValidationSeverity { Info, Warning, Error };
enum class ProjectValidationCode {
    InvalidProject,
    InvalidStartupScene,
    CookDependency,
    ScriptCompile,
    ShaderCompile,
    InvalidMaterial,
    OversizedAsset,
};

struct ProjectValidationIssue {
    ProjectValidationSeverity severity = ProjectValidationSeverity::Error;
    ProjectValidationCode code = ProjectValidationCode::InvalidProject;
    std::string path;
    std::string referrer;
    std::string message;
};

struct ProjectValidationOptions {
    uint64_t oversizedAssetWarningBytes = 64ull * 1024ull * 1024ull;
};

struct ProjectValidationReport {
    std::vector<ProjectValidationIssue> issues;
    PublishPreflightReport preflight;
    uint64_t contentBytes = 0;
    uint32_t scannedFiles = 0;

    size_t ErrorCount() const;
    size_t WarningCount() const;
    bool Passed() const { return ErrorCount() == 0; }
    std::string Summary() const;
};

class ProjectValidator {
public:
    static bool Validate(const ProjectConfig& project, ProjectValidationReport& report,
                         const ProjectValidationOptions& options = {});
};
