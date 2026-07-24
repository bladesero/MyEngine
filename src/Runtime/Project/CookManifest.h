#pragma once

#include "API/RuntimeApi.h"

#include "Project/ContentArchive.h"
#include "Project/PublishTargets.h"
#include "Project/FormatVersions.h"
#include "Core/BuildInfo.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MYENGINE_RUNTIME_API CookManifest {
    static constexpr int kCurrentVersion = FormatVersions::CookManifest;
    static constexpr const char* kFileName = "CookManifest.json";

    int version = kCurrentVersion;
    std::string project;
    std::string projectId;
    std::string engineVersion;
    std::string buildId;
    std::string gitCommit = std::string(BuildInfo::GitCommit);
    std::string compiler = std::string(BuildInfo::Compiler);
    std::string shaderToolVersion = std::string(BuildInfo::ShaderTool);
    int contentSchemaVersion = 0;
    int archiveFormatVersion = 0;
    std::string hashAlgorithm = "sha256";
    std::string configuration;
    std::vector<std::string> requiredBackends;
    std::string runtimeDependenciesHash;
    std::string target = PublishTargets::kDefaultTargetId;
    std::string startupScene;
    std::string archive = ContentArchive::kFileName;
    std::string archiveHash;
    uint64_t contentBytes = 0;
    std::vector<CookedContentEntry> files;

    bool Validate(std::string* error = nullptr) const;
    bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
    static bool Load(const std::filesystem::path& path, CookManifest& manifest, std::string* error = nullptr);
};
