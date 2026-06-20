#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct CookedContentEntry {
    std::string path;
    uint64_t size = 0;
    std::string hash;
};

class ContentArchive {
public:
    static constexpr const char* kFileName = "Content.pak";

    static bool Create(const std::filesystem::path& contentRoot,
                       const std::filesystem::path& archivePath,
                       std::vector<CookedContentEntry>* entries = nullptr,
                       std::string* error = nullptr);
    static bool Extract(const std::filesystem::path& archivePath,
                        const std::filesystem::path& projectRoot,
                        std::string* error = nullptr);
    static std::string HashFile(const std::filesystem::path& path,
                                std::string* error = nullptr);
};
