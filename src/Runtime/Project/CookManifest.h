#pragma once

#include "Project/ContentArchive.h"
#include "Project/PublishTargets.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CookManifest {
    static constexpr int kCurrentVersion = 2;
    static constexpr const char* kFileName = "CookManifest.json";

    int version = kCurrentVersion;
    std::string project;
    std::string projectId;
    std::string engineVersion;
    std::string buildId;
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
    bool Save(const std::filesystem::path& path,
              std::string* error = nullptr) const;
    static bool Load(const std::filesystem::path& path,
                     CookManifest& manifest,
                     std::string* error = nullptr);
};
