#pragma once

#include "Core/RuntimeFileSystem.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct CookedContentEntry {
    std::string path;
    uint64_t size = 0;
    std::string hash;
};

class ContentArchive {
public:
    static constexpr const char* kFileName = "Content.pak";

    static bool Create(const std::filesystem::path& contentRoot, const std::filesystem::path& archivePath,
                       std::vector<CookedContentEntry>* entries = nullptr, std::string* error = nullptr);
    static bool Extract(const std::filesystem::path& archivePath, const std::filesystem::path& projectRoot,
                        std::string* error = nullptr);
    static std::string HashFile(const std::filesystem::path& path, std::string* error = nullptr);
};

struct CookManifest;

class ContentArchiveReader {
public:
    bool Open(const std::filesystem::path& archivePath, std::string* error = nullptr);
    bool Exists(const std::string& path) const;
    uint64_t FileSize(const std::string& path) const;
    bool Stat(const std::string& path, FileStat& out, std::string* error = nullptr) const;
    std::vector<std::string> ListFiles(const std::string& prefix = {}) const;
    bool ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr) const;
    bool ReadFileUncheckedAfterManifestValidated(const std::string& path, std::vector<uint8_t>& out,
                                                 std::string* error = nullptr) const;
    bool ValidateAgainstManifest(const CookManifest& manifest, std::string* error = nullptr) const;
    size_t EntryCount() const { return m_Entries.size(); }
    uint64_t ContentBytes() const { return m_ContentBytes; }

private:
    struct Entry {
        uint64_t offset = 0;
        uint64_t size = 0;
        std::string hash;
    };

    static std::string NormalizeEntryPath(const std::string& path);
    bool ReadFileInternal(const std::string& path, std::vector<uint8_t>& out, bool verifyHash,
                          std::string* error) const;

    std::filesystem::path m_ArchivePath;
    std::unordered_map<std::string, Entry> m_Entries;
    uint64_t m_ContentBytes = 0;
};

class PakFileSystem final : public IReadOnlyFileSystem {
public:
    explicit PakFileSystem(std::shared_ptr<ContentArchiveReader> reader);

    bool Exists(const std::string& path) const override;
    uint64_t FileSize(const std::string& path) const override;
    bool Stat(const std::string& path, FileStat& out, std::string* error = nullptr) const override;
    std::vector<std::string> ListFiles(const std::string& prefix = {}) const override;
    bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr) const override;
    std::unique_ptr<std::istream> OpenRead(const std::string& path, std::string* error = nullptr) const override;

private:
    std::shared_ptr<ContentArchiveReader> m_Reader;
};
