#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct EditorRecoverySnapshot {
    std::string id;
    std::string scenePath;
    uint64_t revision = 0;
    std::filesystem::path file;
};

class EditorRecoveryService {
public:
    bool OpenProject(const std::filesystem::path& projectRoot, std::string* error = nullptr);
    bool PreviousShutdownWasClean() const { return m_PreviousShutdownClean; }
    bool WriteSnapshot(const std::string& scenePath, uint64_t revision, const std::string& serializedScene,
                       std::string* error = nullptr);
    std::vector<EditorRecoverySnapshot> ListSnapshots(std::string* error = nullptr) const;
    bool ReadSnapshot(const EditorRecoverySnapshot& snapshot, std::string& serializedScene,
                      std::string* error = nullptr) const;
    bool RemoveSnapshot(const std::string& id, std::string* error = nullptr);
    bool RemoveMatchingRevision(const std::string& scenePath, uint64_t revision, std::string* error = nullptr);
    bool MarkCleanShutdown(std::string* error = nullptr);

private:
    bool WriteSessionState(bool clean, std::string* error);
    std::filesystem::path m_RecoveryRoot;
    bool m_PreviousShutdownClean = true;
};
