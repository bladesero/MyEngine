#pragma once

#include "Project/ContentArchive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CookManifest {
    static constexpr int kCurrentVersion = 1;
    static constexpr const char* kFileName = "CookManifest.json";

    int version = kCurrentVersion;
    std::string project;
    std::string target = "windows-x64";
    std::string startupScene;
    std::string archive = ContentArchive::kFileName;
    uint64_t archiveHash = 0;
    uint64_t contentBytes = 0;
    std::vector<CookedContentEntry> files;

    bool Validate(std::string* error = nullptr) const;
    bool Save(const std::filesystem::path& path,
              std::string* error = nullptr) const;
    static bool Load(const std::filesystem::path& path,
                     CookManifest& manifest,
                     std::string* error = nullptr);
};
