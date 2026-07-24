#pragma once

#include "API/RuntimeApi.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ContentFileInfo {
    std::filesystem::path absolute;
    std::filesystem::path relative;
    uint64_t size = 0;
};

struct ContentLimits {
    uint64_t maxFileBytes = 4ull * 1024 * 1024 * 1024;
    uint64_t maxTotalBytes = 64ull * 1024 * 1024 * 1024;
    uint32_t maxFiles = 1000000;
};

class MYENGINE_RUNTIME_API ContentPathPolicy {
public:
    static bool ResolveContained(const std::filesystem::path& root, const std::filesystem::path& candidate,
                                 std::filesystem::path& resolved, std::string* error = nullptr,
                                 bool requireFile = true);
    static bool Enumerate(const std::filesystem::path& root, std::vector<ContentFileInfo>& files, uint64_t& totalBytes,
                          std::string* error = nullptr, const ContentLimits& limits = {});
};
