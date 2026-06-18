#include "Project/ContentArchive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace {
constexpr std::array<char, 8> kMagic = {'M','E','P','A','K','0','1','\0'};
constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

template <typename T>
bool WriteValue(std::ostream& output, const T& value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return output.good();
}

template <typename T>
bool ReadValue(std::istream& input, T& value) {
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return input.good();
}

bool SafeRelativePath(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

uint64_t HashBytes(const char* data, size_t size, uint64_t hash) {
    for (size_t index = 0; index < size; ++index) {
        hash ^= static_cast<unsigned char>(data[index]);
        hash *= kFnvPrime;
    }
    return hash;
}
}

uint64_t ContentArchive::HashFile(const fs::path& path, std::string* error) {
    if (error) error->clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open file for hashing: " + path.string());
        return 0;
    }
    std::array<char, 64 * 1024> buffer{};
    uint64_t hash = kFnvOffset;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<size_t>(input.gcount());
        hash = HashBytes(buffer.data(), count, hash);
    }
    if (!input.eof()) {
        SetError(error, "failed while hashing: " + path.string());
        return 0;
    }
    return hash;
}

bool ContentArchive::Create(const fs::path& contentRoot, const fs::path& archivePath,
                            std::vector<CookedContentEntry>* entries,
                            std::string* error) {
    if (error) error->clear();
    std::error_code ec;
    if (!fs::is_directory(contentRoot, ec) || ec) {
        SetError(error, "Content directory does not exist: " + contentRoot.string());
        return false;
    }

    std::vector<std::pair<fs::path, fs::path>> files;
    for (fs::recursive_directory_iterator it(contentRoot, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        const fs::path relative = fs::relative(it->path(), contentRoot, ec);
        if (ec || !SafeRelativePath(relative)) {
            SetError(error, "invalid Content path: " + it->path().string());
            return false;
        }
        files.emplace_back(relative, it->path());
    }
    if (ec) {
        SetError(error, "failed to enumerate Content: " + ec.message());
        return false;
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.first.generic_string() < right.first.generic_string();
    });
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
        const uint64_t hash = HashFile(source, &hashError);
        if (!hashError.empty()) {
            SetError(error, hashError);
            return false;
        }
        if (!WriteValue(output, pathLength) || !WriteValue(output, size) ||
            !WriteValue(output, hash)) {
            SetError(error, "failed to write archive entry header");
            return false;
        }
        output.write(storedPath.data(), static_cast<std::streamsize>(storedPath.size()));
        std::ifstream input(source, std::ios::binary);
        uint64_t remaining = size;
        while (remaining > 0 && input) {
            const size_t request = static_cast<size_t>((std::min)(
                remaining, static_cast<uint64_t>(buffer.size())));
            input.read(buffer.data(), static_cast<std::streamsize>(request));
            const auto count = static_cast<size_t>(input.gcount());
            if (count == 0) break;
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
    if (entries) *entries = std::move(cooked);
    return true;
}

bool ContentArchive::Extract(const fs::path& archivePath, const fs::path& projectRoot,
                             std::string* error) {
    if (error) error->clear();
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
    for (uint32_t index = 0; index < fileCount; ++index) {
        uint32_t pathLength = 0;
        uint64_t size = 0;
        uint64_t expectedHash = 0;
        if (!ReadValue(input, pathLength) || !ReadValue(input, size) ||
            !ReadValue(input, expectedHash) || pathLength == 0 || pathLength > 1024 * 1024) {
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
        uint64_t actualHash = kFnvOffset;
        while (remaining > 0) {
            const size_t request = static_cast<size_t>((std::min)(
                remaining, static_cast<uint64_t>(buffer.size())));
            input.read(buffer.data(), static_cast<std::streamsize>(request));
            const auto count = static_cast<size_t>(input.gcount());
            if (count == 0) break;
            output.write(buffer.data(), static_cast<std::streamsize>(count));
            actualHash = HashBytes(buffer.data(), count, actualHash);
            remaining -= count;
        }
        if (remaining != 0 || !output.good() || actualHash != expectedHash) {
            SetError(error, "Content archive entry is truncated or corrupt: " + storedPath);
            return false;
        }
    }
    return true;
}
