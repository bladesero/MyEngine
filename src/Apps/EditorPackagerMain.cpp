#include "Project/RuntimeDependencies.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool CopyTree(const fs::path& source, const fs::path& destination,
              std::string* error) {
    std::error_code ec;
    if (!fs::is_directory(source, ec) || ec) {
        SetError(error, "directory is missing: " + source.string());
        return false;
    }
    fs::create_directories(destination, ec);
    if (ec) {
        SetError(error, "failed to create directory: " + destination.string());
        return false;
    }
    for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec) {
            SetError(error, "failed to compute relative path under: " + source.string());
            return false;
        }
        const fs::path target = destination / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) {
                SetError(error, "failed to create directory: " + target.string());
                return false;
            }
            continue;
        }
        if (ec || !it->is_regular_file(ec)) continue;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create directory: " + target.parent_path().string());
            return false;
        }
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetError(error, "failed to copy file: " + it->path().string());
            return false;
        }
    }
    if (ec) {
        SetError(error, "failed to enumerate directory: " + source.string());
        return false;
    }
    return true;
}
}

int main(int argc, char* argv[]) {
    fs::path binaries = fs::absolute(argv[0]).parent_path();
    fs::path output = fs::current_path() / "Builds" / "MyEngineEditor-windows-x64";
    fs::path engineContent = fs::current_path() / "EngineContent";
    fs::path projectTemplates = fs::current_path() / "ProjectTemplates";
    fs::path content = binaries / "Content";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto consumeValue = [&](const char* name, fs::path& target) -> bool {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << name << '\n';
                return false;
            }
            target = argv[++index];
            return true;
        };
        if (argument == "--output") {
            if (!consumeValue("--output", output)) return 2;
        } else if (argument == "--binaries") {
            if (!consumeValue("--binaries", binaries)) return 2;
        } else if (argument == "--engine-content") {
            if (!consumeValue("--engine-content", engineContent)) return 2;
        } else if (argument == "--project-templates") {
            if (!consumeValue("--project-templates", projectTemplates)) return 2;
        } else if (argument == "--content") {
            if (!consumeValue("--content", content)) return 2;
        } else if (argument.rfind("--output=", 0) == 0) {
            output = argument.substr(std::string("--output=").size());
        } else if (argument.rfind("--binaries=", 0) == 0) {
            binaries = argument.substr(std::string("--binaries=").size());
        } else if (argument.rfind("--engine-content=", 0) == 0) {
            engineContent = argument.substr(std::string("--engine-content=").size());
        } else if (argument.rfind("--project-templates=", 0) == 0) {
            projectTemplates = argument.substr(std::string("--project-templates=").size());
        } else if (argument.rfind("--content=", 0) == 0) {
            content = argument.substr(std::string("--content=").size());
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    binaries = fs::absolute(binaries);
    output = fs::absolute(output);
    engineContent = fs::absolute(engineContent);
    projectTemplates = fs::absolute(projectTemplates);
    content = fs::absolute(content);

    std::error_code ec;
    const std::vector<std::string> executables = {
        "MyEngineEditor.exe",
        "MyEnginePlayer.exe",
        "MyEngineCooker.exe"
    };
    for (const std::string& executable : executables) {
        ec.clear();
        if (!fs::is_regular_file(binaries / executable, ec) || ec) {
            std::cerr << "Required binary is missing: " << (binaries / executable).string() << '\n';
            return 1;
        }
    }

    fs::remove_all(output, ec);
    ec.clear();
    fs::create_directories(output, ec);
    if (ec) {
        std::cerr << "Failed to create output directory: " << output.string() << '\n';
        return 1;
    }

    RuntimeDependencyManifest runtimeDependencies;
    std::string error;
    if (!WindowsRuntimeDependencyCollector::Collect(
            binaries, output, runtimeDependencies, executables, &error) ||
        !runtimeDependencies.Save(output / RuntimeDependencyManifest::kFileName, &error)) {
        std::cerr << "Packaging error: " << error << '\n';
        return 1;
    }
    if (!CopyTree(engineContent, output / "EngineContent", &error)) {
        std::cerr << "Packaging error: " << error << '\n';
        return 1;
    }
    if (fs::is_directory(projectTemplates, ec) && !ec &&
        !CopyTree(projectTemplates, output / "ProjectTemplates", &error)) {
        std::cerr << "Packaging error: " << error << '\n';
        return 1;
    }
    if (fs::is_directory(content, ec) && !ec &&
        !CopyTree(content, output / "Content", &error)) {
        std::cerr << "Packaging error: " << error << '\n';
        return 1;
    }

    std::cout << "Packaged MyEngineEditor to " << output.string() << '\n';
    std::cout << "Bundled " << runtimeDependencies.files.size()
              << " runtime dependencies" << '\n';
    return 0;
}