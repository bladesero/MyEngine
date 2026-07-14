#include "Project/ContentArchive.h"
#include "Project/ContentPathPolicy.h"
#include "Project/CookManifest.h"
#include "Core/Sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <streambuf>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
constexpr std::array<char, 8> kMagic = {'M', 'E', 'P', 'A', 'K', '0', '2', '\0'};

void SetError(std::string* error, std::string message) {
    if (error)
        *error = std::move(message);
}

template <typename T> bool WriteValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return output.good();
}

template <typename T> bool ReadValue(std::istream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return input.good();
}

bool SafeRelativePath(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    for (const auto& part : path) {
        if (part == "..")
            return false;
    }
    return true;
}

class MemoryReadBuffer final : public std::streambuf {
public:
    explicit MemoryReadBuffer(std::vector<uint8_t> bytes) : m_Bytes(std::move(bytes)) {
        if (m_Bytes.empty()) {
            setg(nullptr, nullptr, nullptr);
            return;
        }
        char* begin = reinterpret_cast<char*>(m_Bytes.data());
        setg(begin, begin, begin + static_cast<std::ptrdiff_t>(m_Bytes.size()));
    }

private:
    std::vector<uint8_t> m_Bytes;
};

class MemoryInputStream final : public std::istream {
public:
    explicit MemoryInputStream(std::vector<uint8_t> bytes) : std::istream(nullptr), m_Buffer(std::move(bytes)) {
        rdbuf(&m_Buffer);
    }

private:
    MemoryReadBuffer m_Buffer;
};

} // namespace

std::string ContentArchive::HashFile(const fs::path& path, std::string* error) {
    return Sha256::HashFile(path, error);
}

bool ContentArchive::Create(const fs::path& contentRoot, const fs::path& archivePath,
                            std::vector<CookedContentEntry>* entries, std::string* error) {
    if (error)
        error->clear();
    std::error_code ec;
    if (!fs::is_directory(contentRoot, ec) || ec) {
        SetError(error, "Content directory does not exist: " + contentRoot.string());
        return false;
    }

    std::vector<ContentFileInfo> contentFiles;
    uint64_t ignoredTotal = 0;
    if (!ContentPathPolicy::Enumerate(contentRoot, contentFiles, ignoredTotal, error))
        return false;
    std::vector<std::pair<fs::path, fs::path>> files;
    for (const auto& file : contentFiles)
        files.emplace_back(file.relative, file.absolute);
    if (files.size() > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "too many files to cook");
        return false;
    }

    fs::create_directories(archivePath.parent_path(), ec);
    if (ec) {
        SetError(error, "failed to create archive directory: " + ec.message());
        return false;
    }
    const fs::path temporary = archivePath.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        SetError(error, "failed to create archive: " + temporary.string());
        return false;
    }
    output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    const uint32_t fileCount = static_cast<uint32_t>(files.size());
    if (!WriteValue(output, fileCount)) {
        SetError(error, "failed to write archive header");
        return false;
    }

    std::vector<CookedContentEntry> cooked;
    cooked.reserve(files.size());
    std::array<char, 64 * 1024> buffer{};
    for (const auto& [relative, source] : files) {
        const std::string storedPath = relative.generic_string();
        const uint32_t pathLength = static_cast<uint32_t>(storedPath.size());
        const uint64_t size = fs::file_size(source, ec);
        if (ec || pathLength == 0) {
            SetError(error, "failed to inspect content file: " + source.string());
            return false;
        }
        std::string hashError;
        const std::string hash = HashFile(source, &hashError);
        Sha256::Digest digest{};
        if (!hashError.empty()) {
            SetError(error, hashError);
            return false;
        }
        if (!hashError.empty() || !Sha256::FromHex(hash, digest) || !WriteValue(output, pathLength) ||
            !WriteValue(output, size)) {
            SetError(error, "failed to write archive entry header");
            return false;
        }
        output.write(reinterpret_cast<const char*>(digest.data()), digest.size());
        output.write(storedPath.data(), static_cast<std::streamsize>(storedPath.size()));
        std::ifstream input(source, std::ios::binary);
        uint64_t remaining = size;
        while (remaining > 0 && input) {
            const size_t request = static_cast<size_t>((std::min)(remaining, static_cast<uint64_t>(buffer.size())));
            input.read(buffer.data(), static_cast<std::streamsize>(request));
            const auto count = static_cast<size_t>(input.gcount());
            if (count == 0)
                break;
            output.write(buffer.data(), static_cast<std::streamsize>(count));
            remaining -= count;
        }
        if (remaining != 0 || !output.good()) {
            SetError(error, "failed to archive content file: " + source.string());
            return false;
        }
        cooked.push_back({"Content/" + storedPath, size, hash});
    }
    output.close();
    if (!output) {
        SetError(error, "failed to finalize archive");
        return false;
    }
    fs::remove(archivePath, ec);
    ec.clear();
    fs::rename(temporary, archivePath, ec);
    if (ec) {
        SetError(error, "failed to install archive: " + ec.message());
        return false;
    }
    if (entries)
        *entries = std::move(cooked);
    return true;
}

bool ContentArchive::Extract(const fs::path& archivePath, const fs::path& projectRoot, std::string* error) {
    if (error)
        error->clear();
    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open Content archive: " + archivePath.string());
        return false;
    }
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    uint32_t fileCount = 0;
    if (!input.good() || magic != kMagic || !ReadValue(input, fileCount) || fileCount > 1000000) {
        SetError(error, "invalid Content archive header");
        return false;
    }

    std::error_code ec;
    const fs::path contentRoot = projectRoot / "Content";
    fs::create_directories(contentRoot, ec);
    if (ec) {
        SetError(error, "failed to create extracted Content directory: " + ec.message());
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    std::unordered_set<std::string> paths;
    uint64_t totalBytes = 0;
    for (uint32_t index = 0; index < fileCount; ++index) {
        uint32_t pathLength = 0;
        uint64_t size = 0;
        Sha256::Digest expectedHash{};
        if (!ReadValue(input, pathLength) || !ReadValue(input, size) ||
            !input.read(reinterpret_cast<char*>(expectedHash.data()), expectedHash.size()) || pathLength == 0 ||
            pathLength > 1024 * 1024 || size > 4ull * 1024 * 1024 * 1024 ||
            size > 64ull * 1024 * 1024 * 1024 - totalBytes) {
            SetError(error, "invalid Content archive entry header");
            return false;
        }
        std::string storedPath(pathLength, '\0');
        input.read(storedPath.data(), static_cast<std::streamsize>(pathLength));
        const fs::path relative = fs::path(storedPath).lexically_normal();
        if (!input.good() || !SafeRelativePath(relative) || !paths.insert(relative.generic_string()).second) {
            SetError(error, "unsafe Content archive entry path");
            return false;
        }
        const fs::path destination = contentRoot / relative;
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create cooked asset directory: " + ec.message());
            return false;
        }
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) {
            SetError(error, "failed to create cooked asset: " + destination.string());
            return false;
        }
        uint64_t remaining = size;
        Sha256 actualHash;
        while (remaining > 0) {
            const size_t request = static_cast<size_t>((std::min)(remaining, static_cast<uint64_t>(buffer.size())));
            input.read(buffer.data(), static_cast<std::streamsize>(request));
            const auto count = static_cast<size_t>(input.gcount());
            if (count == 0)
                break;
            output.write(buffer.data(), static_cast<std::streamsize>(count));
            actualHash.Update(buffer.data(), count);
            remaining -= count;
        }
        if (remaining != 0 || !output.good() || actualHash.Final() != expectedHash) {
            SetError(error, "Content archive entry is truncated or corrupt: " + storedPath);
            return false;
        }
        totalBytes += size;
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        SetError(error, "Content archive contains trailing data");
        return false;
    }
    return true;
}

std::string ContentArchiveReader::NormalizeEntryPath(const std::string& path) {
    if (path.empty())
        return {};
    fs::path normalized = fs::path(path).lexically_normal();
    std::string generic = normalized.generic_string();
    if (generic.rfind("Content/", 0) == 0)
        generic = generic.substr(8);
    if (!SafeRelativePath(fs::path(generic)))
        return {};
    return generic;
}

bool ContentArchiveReader::Open(const fs::path& archivePath, std::string* error) {
    if (error)
        error->clear();
    m_ArchivePath = archivePath;
    m_Entries.clear();
    m_ContentBytes = 0;

    std::ifstream input(archivePath, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open Content archive: " + archivePath.string());
        return false;
    }

    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    uint32_t fileCount = 0;
    if (!input.good() || magic != kMagic || !ReadValue(input, fileCount) || fileCount > 1000000) {
        SetError(error, "invalid Content archive header");
        return false;
    }

    uint64_t totalBytes = 0;
    for (uint32_t index = 0; index < fileCount; ++index) {
        uint32_t pathLength = 0;
        uint64_t size = 0;
        Sha256::Digest expectedHash{};
        if (!ReadValue(input, pathLength) || !ReadValue(input, size) ||
            !input.read(reinterpret_cast<char*>(expectedHash.data()), expectedHash.size()) || pathLength == 0 ||
            pathLength > 1024 * 1024 || size > 4ull * 1024 * 1024 * 1024 ||
            size > 64ull * 1024 * 1024 * 1024 - totalBytes) {
            SetError(error, "invalid Content archive entry header");
            return false;
        }
        std::string storedPath(pathLength, '\0');
        input.read(storedPath.data(), static_cast<std::streamsize>(pathLength));
        const fs::path relative = fs::path(storedPath).lexically_normal();
        if (!input.good() || !SafeRelativePath(relative)) {
            SetError(error, "unsafe Content archive entry path");
            return false;
        }
        const std::string key = relative.generic_string();
        if (m_Entries.count(key)) {
            SetError(error, "duplicate Content archive entry path: " + key);
            return false;
        }
        const std::streampos dataPosition = input.tellg();
        if (dataPosition < 0) {
            SetError(error, "failed to index Content archive");
            return false;
        }
        const auto dataOffset = static_cast<uint64_t>(dataPosition);
        m_Entries.emplace(key, Entry{dataOffset, size, Sha256::ToHex(expectedHash)});
        input.seekg(static_cast<std::streamoff>(size), std::ios::cur);
        if (!input.good()) {
            SetError(error, "Content archive entry is truncated: " + key);
            return false;
        }
        totalBytes += size;
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        SetError(error, "Content archive contains trailing data");
        return false;
    }
    m_ContentBytes = totalBytes;
    return true;
}

bool ContentArchiveReader::Exists(const std::string& path) const {
    const std::string key = NormalizeEntryPath(path);
    return !key.empty() && m_Entries.count(key) > 0;
}

uint64_t ContentArchiveReader::FileSize(const std::string& path) const {
    const std::string key = NormalizeEntryPath(path);
    const auto found = key.empty() ? m_Entries.end() : m_Entries.find(key);
    return found == m_Entries.end() ? 0 : found->second.size;
}

bool ContentArchiveReader::Stat(const std::string& path, FileStat& out, std::string* error) const {
    if (error)
        error->clear();
    const std::string key = NormalizeEntryPath(path);
    if (key.empty()) {
        SetError(error, "unsafe Content archive entry path: " + path);
        return false;
    }
    const auto found = m_Entries.find(key);
    if (found == m_Entries.end()) {
        SetError(error, "Content archive entry not found: " + path);
        return false;
    }
    out = {};
    out.size = found->second.size;
    out.hash = found->second.hash;
    out.sourceKind = "pak";
    out.normalizedPath = (fs::path("Content") / key).generic_string();
    return true;
}

std::vector<std::string> ContentArchiveReader::ListFiles(const std::string& prefix) const {
    std::vector<std::string> files;
    std::string normalizedPrefix;
    if (!prefix.empty()) {
        const std::string genericPrefix = fs::path(prefix).lexically_normal().generic_string();
        if (genericPrefix != "Content" && genericPrefix != "Content/") {
            normalizedPrefix = NormalizeEntryPath(prefix);
            if (normalizedPrefix.empty())
                return files;
        }
    }
    for (const auto& [key, entry] : m_Entries) {
        (void)entry;
        if (!normalizedPrefix.empty() && key.rfind(normalizedPrefix, 0) != 0)
            continue;
        files.push_back((fs::path("Content") / key).generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool ContentArchiveReader::ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) const {
    return ReadFileInternal(path, out, true, error);
}

bool ContentArchiveReader::ReadFileUncheckedAfterManifestValidated(const std::string& path, std::vector<uint8_t>& out,
                                                                   std::string* error) const {
    return ReadFileInternal(path, out, false, error);
}

bool ContentArchiveReader::ReadFileInternal(const std::string& path, std::vector<uint8_t>& out, bool verifyHash,
                                            std::string* error) const {
    if (error)
        error->clear();
    const std::string key = NormalizeEntryPath(path);
    if (key.empty()) {
        SetError(error, "unsafe Content archive entry path: " + path);
        return false;
    }
    const auto found = m_Entries.find(key);
    if (found == m_Entries.end()) {
        SetError(error, "Content archive entry not found: " + path);
        return false;
    }
    std::ifstream input(m_ArchivePath, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open Content archive: " + m_ArchivePath.string());
        return false;
    }
    const Entry& entry = found->second;
    input.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    out.resize(static_cast<size_t>(entry.size));
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    if (!input && !input.eof()) {
        SetError(error, "failed to read Content archive entry: " + key);
        return false;
    }
    if (verifyHash) {
        Sha256 hash;
        if (!out.empty())
            hash.Update(out.data(), out.size());
        if (Sha256::ToHex(hash.Final()) != entry.hash) {
            SetError(error, "Content archive entry hash mismatch: " + key);
            return false;
        }
    }
    return true;
}

bool ContentArchiveReader::ValidateAgainstManifest(const CookManifest& manifest, std::string* error) const {
    if (error)
        error->clear();
    if (manifest.files.size() != m_Entries.size()) {
        SetError(error, "Content archive file count does not match Cook manifest");
        return false;
    }
    for (const CookedContentEntry& manifestEntry : manifest.files) {
        const std::string key = NormalizeEntryPath(manifestEntry.path);
        const auto found = m_Entries.find(key);
        if (found == m_Entries.end()) {
            SetError(error, "Cook manifest entry missing from Content archive: " + manifestEntry.path);
            return false;
        }
        if (found->second.size != manifestEntry.size || found->second.hash != manifestEntry.hash) {
            SetError(error, "Content archive metadata mismatch: " + manifestEntry.path);
            return false;
        }
    }
    return true;
}

PakFileSystem::PakFileSystem(std::shared_ptr<ContentArchiveReader> reader) : m_Reader(std::move(reader)) {
}

bool PakFileSystem::Exists(const std::string& path) const {
    return m_Reader && m_Reader->Exists(path);
}

uint64_t PakFileSystem::FileSize(const std::string& path) const {
    return m_Reader ? m_Reader->FileSize(path) : 0;
}

bool PakFileSystem::Stat(const std::string& path, FileStat& out, std::string* error) const {
    if (!m_Reader) {
        SetError(error, "Content archive is not mounted");
        return false;
    }
    return m_Reader->Stat(path, out, error);
}

std::vector<std::string> PakFileSystem::ListFiles(const std::string& prefix) const {
    return m_Reader ? m_Reader->ListFiles(prefix) : std::vector<std::string>{};
}

bool PakFileSystem::ReadAllBytes(const std::string& path, std::vector<uint8_t>& out, std::string* error) const {
    if (!m_Reader) {
        SetError(error, "Content archive is not mounted");
        return false;
    }
    return m_Reader->ReadFileUncheckedAfterManifestValidated(path, out, error);
}

std::unique_ptr<std::istream> PakFileSystem::OpenRead(const std::string& path, std::string* error) const {
    std::vector<uint8_t> bytes;
    if (!ReadAllBytes(path, bytes, error))
        return {};
    return std::make_unique<MemoryInputStream>(std::move(bytes));
}
