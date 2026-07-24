#pragma once

#include "Project/ContentArchive.h"
#include "Editor/Cooking/CookDependencyGraph.h"
#include "Editor/Cooking/ProjectValidator.h"

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
    PublishPreflightReport preflight;
    ProjectValidationReport validation;
};

class ProjectPublisher {
public:
    static bool Publish(const ProjectConfig& project, const std::filesystem::path& engineBinaryDirectory,
                        const std::filesystem::path& engineContentDirectory, PublishReport& report,
                        std::string* error = nullptr);
};
