#include "Core/RuntimeFileSystem.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
std::shared_ptr<IReadOnlyFileSystem>& ActiveFileSystem() {
    static std::shared_ptr<IReadOnlyFileSystem> fileSystem = std::make_shared<PhysicalFileSystem>();
    return fileSystem;
}

std::mutex& FileSystemMutex() {
    static std::mutex mutex;
    return mutex;
}

void SetError(std::string* error, std::string value) {
    if (error)
        *error = std::move(value);
}

bool IsPackageRootFileName(const std::string& value) {
    static const std::unordered_set<std::string> allowed = {"MyEngine.project.json", "CookManifest.json",
                                                            "RuntimeDependencies.json", "Content.pak"};
    return allowed.count(value) > 0;
}
} // namespace

bool IReadOnlyFileSystem::ReadText(const std::string& path, std::string& out, std::string* error) const {
    std::vector<uint8_t> bytes;
    if (!ReadAllBytes(path, bytes, error))
        return false;
    if (bytes.empty()) {
        out.clear();
        return true;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool PhysicalFileSystem::Exists(const std::string& path) const {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec) && !ec;
}

uint64_t PhysicalFileSystem::FileSize(const std::string& path) const {
    std::error_code ec;
    const uintmax_t size = fs::file_size(fs::path(path), ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

bool PhysicalFileSystem::Stat(const std::string& path, FileStat& out, std::string* error) const {
    std::error_code ec;
    const fs::path file(path);
    if (!fs::is_regular_file(file, ec) || ec) {
        SetError(error, "file not found: " + path);
        return false;
    }
    out = {};
    out.size = static_cast<uint64_t>(fs::file_size(file, ec));
    if (ec) {
        SetError(error, "failed to stat file: " + path);
        return false;
    }
    out.sourceKind = "physical";
    out.normalizedPath = fs::absolute(file, ec).lexically_normal().generic_string();
    if (ec)
        out.normalizedPath = file.lexically_normal().generic_string();
    return true;
}

std::vector<std::string> PhysicalFileSystem::ListFiles(const std::string& prefix) const {
    std::vector<std::string> files;
    if (prefix.empty())
        return files;
    std::error_code ec;
    const fs::path root(prefix);
    if (fs::is_regular_file(root, ec) && !ec) {
        files.push_back(root.lexically_normal().generic_string());
        return files;
    }
    if (!fs::is_directory(root, ec) || ec)
        return files;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec)
            continue;
        files.push_back(it->path().lexically_normal().generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool PhysicalFileSystem::ReadAllBytes(const std::string& path, std::vector<uint8_t>& out, std::string* error) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open file: " + path);
        return false;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        SetError(error, "failed to inspect file: " + path);
        return false;
    }
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    if (!input && !input.eof()) {
        SetError(error, "failed to read file: " + path);
        return false;
    }
    return true;
}

std::unique_ptr<std::istream> PhysicalFileSystem::OpenRead(const std::string& path, std::string* error) const {
    auto input = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*input) {
        SetError(error, "failed to open file: " + path);
        return {};
    }
    return input;
}

PackageRootFileSystem::PackageRootFileSystem(std::filesystem::path root) {
    std::error_code ec;
    m_Root = fs::absolute(std::move(root), ec).lexically_normal();
    if (ec)
        m_Root.clear();
}

std::filesystem::path PackageRootFileSystem::ResolveRootFile(const std::string& path) const {
    if (m_Root.empty() || path.empty())
        return {};
    fs::path input(path);
    if (input.is_absolute() || input.has_root_name() || input.has_root_directory()) {
        std::error_code ec;
        const fs::path relative = fs::relative(input.lexically_normal(), m_Root, ec);
        if (ec || relative.empty() || relative.is_absolute() || relative.begin() == relative.end() ||
            *relative.begin() == "..") {
            return {};
        }
        input = relative;
    }
    input = input.lexically_normal();
    if (input.has_parent_path() || !IsPackageRootFileName(input.generic_string()))
        return {};
    return (m_Root / input).lexically_normal();
}

bool PackageRootFileSystem::Exists(const std::string& path) const {
    const fs::path resolved = ResolveRootFile(path);
    if (resolved.empty())
        return false;
    std::error_code ec;
    return fs::is_regular_file(resolved, ec) && !ec;
}

uint64_t PackageRootFileSystem::FileSize(const std::string& path) const {
    const fs::path resolved = ResolveRootFile(path);
    if (resolved.empty())
        return 0;
    std::error_code ec;
    const uintmax_t size = fs::file_size(resolved, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

bool PackageRootFileSystem::Stat(const std::string& path, FileStat& out, std::string* error) const {
    const fs::path resolved = ResolveRootFile(path);
    if (resolved.empty()) {
        SetError(error, "package root file is not allowed: " + path);
        return false;
    }
    PhysicalFileSystem physical;
    if (!physical.Stat(resolved.string(), out, error))
        return false;
    out.sourceKind = "package";
    out.normalizedPath = resolved.filename().generic_string();
    return true;
}

std::vector<std::string> PackageRootFileSystem::ListFiles(const std::string& prefix) const {
    std::vector<std::string> files;
    const std::string normalizedPrefix = fs::path(prefix).lexically_normal().generic_string();
    for (const char* name : {"MyEngine.project.json", "CookManifest.json", "RuntimeDependencies.json", "Content.pak"}) {
        if (!prefix.empty() && std::string(name).rfind(normalizedPrefix, 0) != 0)
            continue;
        if (Exists(name))
            files.emplace_back(name);
    }
    return files;
}

bool PackageRootFileSystem::ReadAllBytes(const std::string& path, std::vector<uint8_t>& out, std::string* error) const {
    const fs::path resolved = ResolveRootFile(path);
    if (resolved.empty()) {
        SetError(error, "package root file is not allowed: " + path);
        return false;
    }
    return PhysicalFileSystem{}.ReadAllBytes(resolved.string(), out, error);
}

std::unique_ptr<std::istream> PackageRootFileSystem::OpenRead(const std::string& path, std::string* error) const {
    const fs::path resolved = ResolveRootFile(path);
    if (resolved.empty()) {
        SetError(error, "package root file is not allowed: " + path);
        return {};
    }
    return PhysicalFileSystem{}.OpenRead(resolved.string(), error);
}

void MountedFileSystem::SetProjectRoot(std::filesystem::path root) {
    std::error_code ec;
    m_ProjectRoot = root.empty() ? fs::path{} : fs::absolute(std::move(root), ec).lexically_normal();
    if (ec)
        m_ProjectRoot.clear();
}

void MountedFileSystem::AddMount(std::shared_ptr<IReadOnlyFileSystem> mount) {
    if (mount)
        m_Mounts.push_back(std::move(mount));
}

std::string MountedFileSystem::Normalize(const std::string& path) const {
    if (path.empty())
        return path;
    fs::path input(path);
    std::error_code ec;
    if (!m_ProjectRoot.empty()) {
        fs::path absolute = input.is_absolute() || input.has_root_name()
                                ? input.lexically_normal()
                                : fs::absolute(m_ProjectRoot / input, ec).lexically_normal();
        if (!ec) {
            const fs::path relative = fs::relative(absolute, m_ProjectRoot, ec);
            if (!ec && !relative.empty() && !relative.is_absolute() && relative.begin() != relative.end() &&
                *relative.begin() != "..") {
                return relative.generic_string();
            }
        }
        ec.clear();
    }
    return input.lexically_normal().generic_string();
}

bool MountedFileSystem::Exists(const std::string& path) const {
    const std::string normalized = Normalize(path);
    for (const auto& mount : m_Mounts) {
        if (mount->Exists(normalized) || mount->Exists(path))
            return true;
    }
    return false;
}

uint64_t MountedFileSystem::FileSize(const std::string& path) const {
    const std::string normalized = Normalize(path);
    for (const auto& mount : m_Mounts) {
        if (mount->Exists(normalized))
            return mount->FileSize(normalized);
        if (mount->Exists(path))
            return mount->FileSize(path);
    }
    return 0;
}

bool MountedFileSystem::Stat(const std::string& path, FileStat& out, std::string* error) const {
    const std::string normalized = Normalize(path);
    std::string lastError;
    for (const auto& mount : m_Mounts) {
        if (mount->Stat(normalized, out, &lastError))
            return true;
        if (normalized != path && mount->Stat(path, out, &lastError))
            return true;
    }
    SetError(error, "file not found: requested=" + path + " normalized=" + normalized +
                        (lastError.empty() ? std::string{} : " lastError=" + lastError));
    return false;
}

std::vector<std::string> MountedFileSystem::ListFiles(const std::string& prefix) const {
    const std::string normalized = Normalize(prefix);
    std::vector<std::string> files;
    for (const auto& mount : m_Mounts) {
        std::vector<std::string> mountFiles = mount->ListFiles(normalized);
        files.insert(files.end(), mountFiles.begin(), mountFiles.end());
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

bool MountedFileSystem::ReadAllBytes(const std::string& path, std::vector<uint8_t>& out, std::string* error) const {
    const std::string normalized = Normalize(path);
    for (const auto& mount : m_Mounts) {
        if (mount->Exists(normalized))
            return mount->ReadAllBytes(normalized, out, error);
        if (mount->Exists(path))
            return mount->ReadAllBytes(path, out, error);
    }
    SetError(error, "file not found: requested=" + path + " normalized=" + normalized);
    return false;
}

std::unique_ptr<std::istream> MountedFileSystem::OpenRead(const std::string& path, std::string* error) const {
    const std::string normalized = Normalize(path);
    for (const auto& mount : m_Mounts) {
        if (mount->Exists(normalized))
            return mount->OpenRead(normalized, error);
        if (mount->Exists(path))
            return mount->OpenRead(path, error);
    }
    SetError(error, "file not found: requested=" + path + " normalized=" + normalized);
    return {};
}

void RuntimeFileSystem::Set(std::shared_ptr<IReadOnlyFileSystem> fileSystem) {
    std::lock_guard<std::mutex> lock(FileSystemMutex());
    ActiveFileSystem() = fileSystem ? std::move(fileSystem) : std::make_shared<PhysicalFileSystem>();
}

std::shared_ptr<IReadOnlyFileSystem> RuntimeFileSystem::GetShared() {
    std::lock_guard<std::mutex> lock(FileSystemMutex());
    return ActiveFileSystem();
}

const IReadOnlyFileSystem& RuntimeFileSystem::Get() {
    static thread_local std::shared_ptr<IReadOnlyFileSystem> snapshot;
    snapshot = GetShared();
    return *snapshot;
}
