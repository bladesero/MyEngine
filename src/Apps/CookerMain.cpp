#include "Editor/ProjectPublisher.h"
#include "Project/ProjectConfig.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::filesystem::path projectRoot = std::filesystem::current_path();
    std::filesystem::path binaries = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path engineContent = binaries / "EngineContent";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--project" || argument == "--binaries" || argument == "--engine-content") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << argument << '\n';
                return 2;
            }
            const std::string value = argv[++index];
            if (argument == "--project")
                projectRoot = value;
            else if (argument == "--binaries")
                binaries = value;
            else
                engineContent = value;
        } else if (argument.rfind("--project=", 0) == 0) {
            projectRoot = argument.substr(std::string("--project=").size());
        } else if (argument.rfind("--binaries=", 0) == 0) {
            binaries = argument.substr(std::string("--binaries=").size());
        } else if (argument.rfind("--engine-content=", 0) == 0) {
            engineContent = argument.substr(std::string("--engine-content=").size());
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    ProjectConfig project;
    std::string error;
    if (!project.Open(projectRoot, false, &error)) {
        std::cerr << "Project error: " << error << '\n';
        return 1;
    }
    PublishReport report;
    if (!ProjectPublisher::Publish(project, binaries, engineContent, report, &error)) {
        std::cerr << "Publish error: " << error << '\n';
        return 1;
    }
    std::cout << "Published " << report.cookedFiles.size() << " files (" << report.contentBytes << " bytes) to "
              << report.outputDirectory.string() << '\n';
    return 0;
}
