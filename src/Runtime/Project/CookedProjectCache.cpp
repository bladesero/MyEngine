#include "Project/CookedProjectCache.h"

#include "Project/ContentArchive.h"
#include "Project/CookManifest.h"
#include "Project/ProjectConfig.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::atomic_uint64_t g_UniqueCounter{0};

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

std::string UniqueSuffix() {
    const uint64_t value = g_UniqueCounter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << (value & 0xffffffffull);
    return text.str();
}

std::string Prefix(std::string text, size_t count) {
    if (text.size() > count) text.resize(count);
    return text;
}

bool ValidateCache(const fs::path& root, const CookManifest& manifest,
                   const std::string& hashText, std::string* error,
                   bool validateFileHashes = true,
                   const std::string& archiveState = {}) {
    std::ifstream marker(root / ".archive_hash");
    std::string markerHash;
    if (!(marker >> markerHash) || markerHash != hashText) {
        SetError(error, "cooked cache marker is missing or stale");
        return false;
    }
    if (!archiveState.empty()) {
        std::ifstream state(root / ".archive_state");
        std::string markerState;
        if (!(state >> markerState) || markerState != archiveState) {
            SetError(error, "cooked cache archive state is missing or stale");
            return false;
        }
    }
    ProjectConfig config;
    if (!config.Open(root, false, error)) return false;
    if (config.GetName() != manifest.project ||
        config.GetStartupScene() != manifest.startupScene) {
        SetError(error, "cached project config does not match Cook manifest");
        return false;
    }
    for (const auto& entry : manifest.files) {
        const fs::path file = root / fs::path(entry.path);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec) || ec || fs::file_size(file, ec) != entry.size || ec) {
            SetError(error, "cooked cache file is missing or has the wrong size: " + entry.path);
            return false;
        }
        if (validateFileHashes) {
            std::string hashError;
            if (ContentArchive::HashFile(file, &hashError) != entry.hash || !hashError.empty()) {
                SetError(error, "cooked cache file hash mismatch: " + entry.path);
                return false;
            }
        }
    }
    return true;
}

std::string ArchiveState(const fs::path& archive, std::string* error) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(archive, ec);
    if (ec) {
        SetError(error, "failed to read Content archive size: " + ec.message());
        return {};
    }
    const auto writeTime = fs::last_write_time(archive, ec);
    if (ec) {
        SetError(error, "failed to read Content archive timestamp: " + ec.message());
        return {};
    }
    return std::to_string(static_cast<uint64_t>(size)) + ":" +
        std::to_string(writeTime.time_since_epoch().count());
}

void Cleanup(const fs::path& path) {
    std::error_code ignored;
    fs::remove_all(path, ignored);
}

bool WaitForValidCache(const fs::path& root, const CookManifest& manifest,
                       const std::string& hash, std::string* error) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (ValidateCache(root, manifest, hash, error)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

class CacheLock {
public:
    ~CacheLock() {
        if (m_Held) Cleanup(m_Path);
    }

    bool Acquire(const fs::path& path, std::string* error) {
        m_Path = path;
        for (int attempt = 0; attempt < 3000; ++attempt) {
            std::error_code ec;
            if (fs::create_directory(m_Path, ec)) {
                m_Held = true;
                return true;
            }
            if (ec && ec != std::errc::file_exists) {
                SetError(error, "failed to create cooked cache lock: " + ec.message());
                return false;
            }

            ec.clear();
            const auto modified = fs::last_write_time(m_Path, ec);
            if (!ec && fs::file_time_type::clock::now() - modified >
                           std::chrono::minutes(30)) {
                Cleanup(m_Path);
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        SetError(error, "timed out waiting for another process to prepare cooked Content");
        return false;
    }

private:
    fs::path m_Path;
    bool m_Held = false;
};

void CleanupCache(const fs::path& base,const std::string& prefix,const fs::path& current) {
    std::error_code ec; struct Item{fs::path path;fs::file_time_type time;uint64_t bytes=0;};std::vector<Item> items;
    for(fs::directory_iterator it(base,ec),end;it!=end&&!ec;it.increment(ec)){
        const std::string name=it->path().filename().string();
        if((name.rfind(".staging-",0)==0||name.rfind(".stale-",0)==0)&&
           fs::file_time_type::clock::now()-it->last_write_time(ec)>std::chrono::hours(1)){Cleanup(it->path());continue;}
        if(!it->is_directory(ec)||name.rfind(prefix,0)!=0)continue;
        uint64_t bytes=0;for(fs::recursive_directory_iterator f(it->path(),ec),fe;f!=fe&&!ec;f.increment(ec))if(f->is_regular_file(ec))bytes+=f->file_size(ec);
        items.push_back({it->path(),it->last_write_time(ec),bytes});
    }
    std::sort(items.begin(),items.end(),[](const auto&a,const auto&b){return a.time>b.time;});
    uint64_t used=0;size_t kept=0;constexpr uint64_t budget=20ull*1024*1024*1024;
    for(auto& item:items){const bool keep=item.path==current||(kept<3&&used+item.bytes<=budget);if(keep){++kept;used+=item.bytes;}else Cleanup(item.path);}
}
}

fs::path CookedProjectCache::DefaultRoot() {
#if defined(_WIN32)
    char* localAppData = nullptr;
    size_t localAppDataLength = 0;
    if (_dupenv_s(&localAppData, &localAppDataLength, "LOCALAPPDATA") == 0 &&
        localAppData && *localAppData) {
        fs::path root = fs::path(localAppData) / "MyEngine" / "Cooked";
        std::free(localAppData);
        return root;
    }
    if (localAppData) std::free(localAppData);
#endif
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    return ec ? fs::path{} : temp / "MyEngine" / "Cooked";
}

bool CookedProjectCache::Prepare(const fs::path& packageRoot,
                                 CookedProjectMount& mount,
                                 std::string* error) {
    return Prepare(packageRoot, DefaultRoot(), mount, error);
}

bool CookedProjectCache::Prepare(const fs::path& packageRoot,
                                 const fs::path& cacheBase,
                                 CookedProjectMount& mount,
                                 std::string* error) {
    if (error) error->clear();
    mount = {};
    if (cacheBase.empty()) {
        SetError(error, "failed to resolve cooked cache directory");
        return false;
    }

    ProjectConfig packageConfig;
    if (!packageConfig.Open(packageRoot, false, error)) return false;
    CookManifest manifest;
    if (!CookManifest::Load(packageRoot / CookManifest::kFileName, manifest, error)) return false;
    if(manifest.engineVersion!=RuntimeCompatibility::kEngineVersion ||
       manifest.buildId!=RuntimeCompatibility::kBuildId ||
       manifest.contentSchemaVersion!=RuntimeCompatibility::kContentSchemaVersion ||
       manifest.archiveFormatVersion!=RuntimeCompatibility::kArchiveFormatVersion ||
       manifest.configuration!=RuntimeCompatibility::kConfiguration) {
        SetError(error,"package is incompatible with this Player build"); return false;
    }
    if (manifest.project != packageConfig.GetName() ||
        manifest.startupScene != packageConfig.GetStartupScene()) {
        SetError(error, "project config does not match Cook manifest");
        return false;
    }

    RuntimeDependencyManifest dependencies;
    const fs::path dependencyPath=packageRoot/RuntimeDependencyManifest::kFileName;
    std::string hashError;
    const std::string dependencyManifestHash=ContentArchive::HashFile(dependencyPath,&hashError);
    if(!hashError.empty()){SetError(error,hashError);return false;}
    if(dependencyManifestHash!=manifest.runtimeDependenciesHash){
        SetError(error,"RuntimeDependencies.json SHA-256 does not match Cook manifest");return false;
    }
    if(!RuntimeDependencyManifest::Load(dependencyPath,dependencies,error) ||
       !dependencies.ValidateFiles(packageRoot,error)) return false;
    const std::string hash = manifest.archiveHash;
    const std::string cacheHash = Prefix(hash, 16);
    const std::string projectPrefix = "p-" + Prefix(manifest.projectId, 8) + "-";
    const fs::path finalRoot = cacheBase / (projectPrefix + cacheHash);
    const fs::path archive = packageRoot / manifest.archive;
    const std::string archiveState = ArchiveState(archive, error);
    if (archiveState.empty()) return false;
    std::string validationError;
    if (ValidateCache(finalRoot, manifest, hash, &validationError, false, archiveState)) {
        mount.projectRoot = finalRoot;
        return true;
    }

    const std::string actualArchiveHash = ContentArchive::HashFile(archive, &hashError);
    if (!hashError.empty()) {
        SetError(error, hashError);
        return false;
    }
    if (actualArchiveHash != manifest.archiveHash) {
        SetError(error, "Content archive hash does not match Cook manifest");
        return false;
    }

    std::error_code ec;
    fs::create_directories(cacheBase, ec);
    if (ec) {
        SetError(error, "failed to create cooked cache root: " + ec.message());
        return false;
    }
    const auto space=fs::space(cacheBase,ec);
    constexpr uint64_t reserve=64ull*1024*1024;
    if(ec || manifest.contentBytes>space.available || reserve>space.available-manifest.contentBytes) {
        SetError(error,"insufficient disk space for cooked Content cache"); return false;
    }
    CleanupCache(cacheBase,projectPrefix,finalRoot);
    CacheLock lock;
    if (!lock.Acquire(cacheBase / (".l-" + cacheHash), error)) return false;

    // The process that held the lock before us may have completed the cache.
    if (ValidateCache(finalRoot, manifest, hash, &validationError)) {
        mount.projectRoot = finalRoot;
        return true;
    }
    const std::string suffix = UniqueSuffix();
    const fs::path staging = cacheBase / (".s-" + cacheHash + "-" + suffix);
    const fs::path stale = cacheBase / (".x-" + cacheHash + "-" + suffix);
    Cleanup(staging);
    fs::create_directories(staging, ec);
    if (ec) {
        SetError(error, "failed to create cooked cache staging directory: " + ec.message());
        return false;
    }

    fs::copy_file(packageConfig.GetManifestPath(), staging / ProjectConfig::kFileName,
                  fs::copy_options::overwrite_existing, ec);
    if (ec || !ContentArchive::Extract(archive, staging, error)) {
        if (ec) SetError(error, "failed to copy project config into cooked cache: " + ec.message());
        Cleanup(staging);
        return false;
    }
    {
        std::ofstream marker(staging / ".archive_hash", std::ios::trunc);
        marker << hash << '\n';
        if (!marker.good()) {
            SetError(error, "failed to finalize cooked cache marker");
            Cleanup(staging);
            return false;
        }
    }
    {
        std::ofstream state(staging / ".archive_state", std::ios::trunc);
        state << archiveState << '\n';
        if (!state.good()) {
            SetError(error, "failed to finalize cooked cache archive state");
            Cleanup(staging);
            return false;
        }
    }
    if (!ValidateCache(staging, manifest, hash, error)) {
        Cleanup(staging);
        return false;
    }

    // Another process may have completed the same cache while this process extracted.
    if (ValidateCache(finalRoot, manifest, hash, &validationError)) {
        Cleanup(staging);
        mount.projectRoot = finalRoot;
        return true;
    }

    if (fs::exists(finalRoot, ec) && !ec) {
        fs::rename(finalRoot, stale, ec);
        if (ec) {
            // A concurrent process may be replacing the cache. Re-check before failing.
            const std::string renameError = ec.message();
            if (WaitForValidCache(finalRoot, manifest, hash, &validationError)) {
                Cleanup(staging);
                mount.projectRoot = finalRoot;
                return true;
            }
            SetError(error, "failed to quarantine stale cooked cache: " + renameError);
            Cleanup(staging);
            return false;
        }
    }
    ec.clear();
    fs::rename(staging, finalRoot, ec);
    if (ec) {
        const std::string renameError = ec.message();
        if (WaitForValidCache(finalRoot, manifest, hash, &validationError)) {
            Cleanup(staging);
            Cleanup(stale);
            mount.projectRoot = finalRoot;
            return true;
        }
        SetError(error, "failed to install cooked cache: " + renameError);
        Cleanup(staging);
        if (fs::exists(stale)) {
            std::error_code restoreError;
            fs::rename(stale, finalRoot, restoreError);
        }
        return false;
    }
    Cleanup(stale);
    mount.projectRoot = finalRoot;
    mount.rebuilt = true;
    return true;
}
