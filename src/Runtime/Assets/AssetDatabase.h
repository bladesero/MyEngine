#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class AssetImportState { Ready, Importing, Failed, Stale, MissingSource };

struct AssetDiagnostic {
    std::string severity;
    std::string message;
};

struct AssetRecord {
    std::string uuid;
    std::string sourcePath;
    std::string artifactPath;
    std::string type;
    std::string importer;
    uint32_t importerVersion = 1;
    std::string sourceHash;
    std::string artifactHash;
    std::string settingsJson = "{}";
    std::vector<std::string> dependencies;
    AssetImportState state = AssetImportState::Ready;
    std::vector<AssetDiagnostic> diagnostics;
    bool alwaysCook = false;
};

class AssetDatabase {
public:
    static constexpr uint32_t kVersion = 1;

    bool Open(std::filesystem::path databasePath, std::string* error = nullptr);
    bool Save(std::string* error = nullptr) const;
    bool Upsert(AssetRecord record, std::string* error = nullptr);
    bool Remove(const std::string& uuid);
    void Clear();

    const AssetRecord* FindByUuid(const std::string& uuid) const;
    const AssetRecord* FindBySourcePath(const std::string& sourcePath) const;
    std::vector<const AssetRecord*> GetDependencies(const std::string& uuid) const;
    std::vector<const AssetRecord*> GetReferencers(const std::string& uuid) const;
    std::vector<AssetRecord> GetAll() const;
    bool Validate(std::string* error = nullptr) const;

    const std::filesystem::path& GetPath() const { return m_Path; }

private:
    void RebuildIndexes();
    static std::string NormalizePath(const std::string& path);

    std::filesystem::path m_Path;
    std::unordered_map<std::string, AssetRecord> m_Records;
    std::unordered_map<std::string, std::string> m_SourceToUuid;
    std::unordered_map<std::string, std::vector<std::string>> m_Referencers;
};
