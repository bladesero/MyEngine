#pragma once

#include "Project/ContentArchive.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class ProjectConfig;

struct PublishReport {
    std::filesystem::path outputDirectory;
    std::filesystem::path contentArchive;
    uint64_t contentBytes = 0;
    std::vector<CookedContentEntry> cookedFiles;
};

class ProjectPublisher {
public:
    static bool Publish(const ProjectConfig& project,
                        const std::filesystem::path& engineBinaryDirectory,
                        PublishReport& report,
                        std::string* error = nullptr);
};
