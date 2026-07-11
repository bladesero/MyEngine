#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <string>
#include <vector>

struct FileStat {
    uint64_t size = 0;
    std::string hash;
    std::string sourceKind;
    std::string normalizedPath;
};

class IReadOnlyFileSystem {
public:
    virtual ~IReadOnlyFileSystem() = default;
    virtual bool Exists(const std::string& path) const = 0;
    virtual uint64_t FileSize(const std::string& path) const = 0;
    virtual bool Stat(const std::string& path, FileStat& out,
                      std::string* error = nullptr) const = 0;
    virtual std::vector<std::string> ListFiles(
        const std::string& prefix = {}) const = 0;
    virtual bool ReadAllBytes(const std::string& path,
                              std::vector<uint8_t>& out,
                              std::string* error = nullptr) const = 0;
    virtual std::unique_ptr<std::istream> OpenRead(
        const std::string& path,
        std::string* error = nullptr) const = 0;

    bool ReadText(const std::string& path, std::string& out,
                  std::string* error = nullptr) const;
};

class PhysicalFileSystem final : public IReadOnlyFileSystem {
public:
    bool Exists(const std::string& path) const override;
    uint64_t FileSize(const std::string& path) const override;
    bool Stat(const std::string& path, FileStat& out,
              std::string* error = nullptr) const override;
    std::vector<std::string> ListFiles(
        const std::string& prefix = {}) const override;
    bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& out,
                      std::string* error = nullptr) const override;
    std::unique_ptr<std::istream> OpenRead(
        const std::string& path,
        std::string* error = nullptr) const override;
};

class PackageRootFileSystem final : public IReadOnlyFileSystem {
public:
    explicit PackageRootFileSystem(std::filesystem::path root);

    bool Exists(const std::string& path) const override;
    uint64_t FileSize(const std::string& path) const override;
    bool Stat(const std::string& path, FileStat& out,
              std::string* error = nullptr) const override;
    std::vector<std::string> ListFiles(
        const std::string& prefix = {}) const override;
    bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& out,
                      std::string* error = nullptr) const override;
    std::unique_ptr<std::istream> OpenRead(
        const std::string& path,
        std::string* error = nullptr) const override;

private:
    std::filesystem::path ResolveRootFile(const std::string& path) const;

    std::filesystem::path m_Root;
};

class MountedFileSystem final : public IReadOnlyFileSystem {
public:
    void SetProjectRoot(std::filesystem::path root);
    void AddMount(std::shared_ptr<IReadOnlyFileSystem> mount);

    bool Exists(const std::string& path) const override;
    uint64_t FileSize(const std::string& path) const override;
    bool Stat(const std::string& path, FileStat& out,
              std::string* error = nullptr) const override;
    std::vector<std::string> ListFiles(
        const std::string& prefix = {}) const override;
    bool ReadAllBytes(const std::string& path, std::vector<uint8_t>& out,
                      std::string* error = nullptr) const override;
    std::unique_ptr<std::istream> OpenRead(
        const std::string& path,
        std::string* error = nullptr) const override;

private:
    std::string Normalize(const std::string& path) const;

    std::filesystem::path m_ProjectRoot;
    std::vector<std::shared_ptr<IReadOnlyFileSystem>> m_Mounts;
};

class RuntimeFileSystem {
public:
    static void Set(std::shared_ptr<IReadOnlyFileSystem> fileSystem);
    static std::shared_ptr<IReadOnlyFileSystem> GetShared();
    static const IReadOnlyFileSystem& Get();
};
