#pragma once

#include "API/RuntimeApi.h"
#include "Project/FormatVersions.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct RuntimeDependencyEntry {
    std::string file;
    std::string architecture = "x64";
    uint64_t size = 0;
    std::string hash;
};
struct MYENGINE_RUNTIME_API RuntimeDependencyManifest {
    static constexpr int kVersion = FormatVersions::RuntimeDependencies;
    static constexpr const char* kFileName = "RuntimeDependencies.json";
    std::vector<RuntimeDependencyEntry> files;
    bool Save(const std::filesystem::path&, std::string* error = nullptr) const;
    static bool Load(const std::filesystem::path&, RuntimeDependencyManifest&, std::string* error = nullptr);
    bool ValidateFiles(const std::filesystem::path&, std::string* error = nullptr) const;
    static bool ValidatePackage(const std::filesystem::path& packageRoot, std::string* error = nullptr);
};

class MYENGINE_RUNTIME_API WindowsRuntimeDependencyCollector {
public:
    static bool Collect(const std::filesystem::path& binaryDirectory, const std::filesystem::path& stagingDirectory,
                        RuntimeDependencyManifest& manifest, const std::vector<std::string>& executableNames,
                        std::string* error = nullptr);
    static bool Collect(const std::filesystem::path& binaryDirectory, const std::filesystem::path& stagingDirectory,
                        RuntimeDependencyManifest& manifest, const std::string& executableName,
                        std::string* error = nullptr);
    static bool Collect(const std::filesystem::path& binaryDirectory, const std::filesystem::path& stagingDirectory,
                        RuntimeDependencyManifest& manifest, std::string* error = nullptr);
};

class HostRuntimeDependencyCollector {
public:
    static bool Collect(const std::filesystem::path& binaryDirectory, const std::filesystem::path& stagingDirectory,
                        RuntimeDependencyManifest& manifest, const std::vector<std::string>& fileNames,
                        std::string* error = nullptr);
};
