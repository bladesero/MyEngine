#include "Editor/ProjectPublisher.h"

#include "Project/CookManifest.h"
#include "Project/ProjectConfig.h"

#include <cctype>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string SafeName(std::string name) {
    for (char& value : name) {
        const unsigned char c = static_cast<unsigned char>(value);
        if (!std::isalnum(c) && value != '-' && value != '_') value = '_';
    }
    return name.empty() ? "Project" : name;
}

bool CopyRequired(const fs::path& source, const fs::path& destination,
                  std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(source, ec) || ec) {
        SetError(error, "required runtime file is missing: " + source.string());
        return false;
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError(error, "failed to copy runtime file: " + source.string() + ": " + ec.message());
        return false;
    }
    return true;
}

bool IsWithin(const fs::path& path, const fs::path& parent) {
    std::error_code ec;
    const fs::path relative = fs::relative(path, parent, ec);
    return !ec && !relative.empty() && !relative.is_absolute() &&
        relative.begin() != relative.end() && *relative.begin() != "..";
}

void Cleanup(const fs::path& path) {
    std::error_code ignored;
    fs::remove_all(path, ignored);
}
}

bool ProjectPublisher::Publish(const ProjectConfig& project,
                               const fs::path& engineBinaryDirectory,
                               PublishReport& report,
                               std::string* error) {
    if (error) error->clear();
    report = {};
    fs::path startup;
    if (!project.ResolveStartupScene(startup, error)) return false;

    const auto& settings = project.GetPublishSettings();
    if (settings.target != "windows-x64") {
        SetError(error, "unsupported publish target: " + settings.target);
        return false;
    }
#ifndef _WIN32
    SetError(error, "windows-x64 publishing is only available on Windows");
    return false;
#endif
    std::error_code ec;
    if (!fs::is_directory(project.GetRoot() / "Content", ec) || ec) {
        SetError(error, "project Content directory is missing");
        return false;
    }
    fs::path outputBase(settings.outputDirectory);
    if (!outputBase.is_absolute()) outputBase = project.GetRoot() / outputBase;
    outputBase = outputBase.lexically_normal();
    const fs::path finalDirectory = outputBase /
        (SafeName(project.GetName()) + "-" + SafeName(settings.target));
    const fs::path staging = finalDirectory.string() + ".staging";
    const fs::path backup = finalDirectory.string() + ".backup";
    if (finalDirectory == project.GetRoot() ||
        IsWithin(finalDirectory, project.GetRoot() / "Content")) {
        SetError(error, "publish directory must not replace or be inside the project Content root");
        return false;
    }

    // Recover a previous interrupted restore before starting another publish.
    const bool finalExists = fs::exists(finalDirectory, ec) && !ec;
    ec.clear();
    if (fs::exists(backup, ec) && !ec) {
        if (!finalExists) {
            fs::rename(backup, finalDirectory, ec);
            if (ec) {
                SetError(error, "failed to restore interrupted publish backup: " + ec.message());
                return false;
            }
        } else {
            Cleanup(backup);
        }
    }

#ifdef _WIN32
    const char* required[] = {"MyEnginePlayer.exe", "runtime.dll", "SDL3.dll"};
#elif defined(__APPLE__)
    const char* required[] = {"MyEnginePlayer", "libruntime.dylib", "libSDL3.dylib"};
#else
    const char* required[] = {"MyEnginePlayer", "libruntime.so", "libSDL3.so"};
#endif
    // Preflight every required input before touching an existing successful package.
    for (const char* name : required) {
        if (!fs::is_regular_file(engineBinaryDirectory / name, ec) || ec) {
            SetError(error, "required runtime file is missing: " +
                     (engineBinaryDirectory / name).string());
            return false;
        }
    }
    if (!fs::is_regular_file(project.GetManifestPath(), ec) || ec) {
        SetError(error, "project manifest is missing: " + project.GetManifestPath().string());
        return false;
    }

    Cleanup(staging);
    fs::create_directories(staging, ec);
    if (ec) {
        SetError(error, "failed to create publish staging directory: " + ec.message());
        return false;
    }

    for (const char* name : required) {
        if (!CopyRequired(engineBinaryDirectory / name, staging / name, error)) {
            Cleanup(staging);
            return false;
        }
    }
    if (!CopyRequired(project.GetManifestPath(), staging / ProjectConfig::kFileName, error)) {
        Cleanup(staging);
        return false;
    }

    std::vector<CookedContentEntry> entries;
    const fs::path archive = staging / ContentArchive::kFileName;
    if (!ContentArchive::Create(project.GetRoot() / "Content", archive, &entries, error)) {
        Cleanup(staging);
        return false;
    }
    uint64_t contentBytes = 0;
    for (const auto& entry : entries) {
        contentBytes += entry.size;
    }
    std::string hashError;
    const uint64_t archiveHash = ContentArchive::HashFile(archive, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        Cleanup(staging);
        return false;
    }
    CookManifest manifest;
    manifest.project = project.GetName();
    manifest.target = settings.target;
    manifest.startupScene = project.GetStartupScene();
    manifest.archiveHash = archiveHash;
    manifest.contentBytes = contentBytes;
    manifest.files = entries;
    if (!manifest.Save(staging / CookManifest::kFileName, error)) {
        Cleanup(staging);
        return false;
    }

    bool hasBackup = false;
    if (fs::exists(finalDirectory, ec) && !ec) {
        fs::rename(finalDirectory, backup, ec);
        if (ec) {
            SetError(error, "failed to back up the previous published project: " + ec.message());
            Cleanup(staging);
            return false;
        }
        hasBackup = true;
    }
    fs::rename(staging, finalDirectory, ec);
    if (ec) {
        const std::string installError = ec.message();
        if (hasBackup) {
            std::error_code restoreError;
            fs::rename(backup, finalDirectory, restoreError);
            if (restoreError) {
                SetError(error, "failed to install published project: " + installError +
                         "; failed to restore previous package: " + restoreError.message());
                Cleanup(staging);
                return false;
            }
        }
        SetError(error, "failed to install published project: " + installError);
        Cleanup(staging);
        return false;
    }
    Cleanup(backup);
    report.outputDirectory = finalDirectory;
    report.contentArchive = finalDirectory / ContentArchive::kFileName;
    report.contentBytes = contentBytes;
    report.cookedFiles = std::move(entries);
    return true;
}
