#include "Project/SaveGame.h"

#include "Assets/AssetManager.h"
#include "Core/BuildInfo.h"
#include "Core/TransactionalFileWriter.h"
#include "Project/JsonMigrationRegistry.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;
namespace {
std::mutex g_StorageMutex;
fs::path g_StorageRoot;

void SetError(std::string* error, const std::string& value) {
    if (error)
        *error = value;
}

bool IsValidSlot(const std::string& slot, std::string* error) {
    fs::path value(slot);
    if (slot.empty() || value.is_absolute() || value.has_parent_path() || value.extension() != ".json") {
        SetError(error, "save slot must be a filename ending in .json");
        return false;
    }
    for (char ch : slot)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.')) {
            SetError(error, "save slot contains invalid characters");
            return false;
        }
    return true;
}

bool IsValidCheckpointId(const std::string& value, std::string* error) {
    if (value.empty() || value.size() > 64) {
        SetError(error, "checkpoint id must contain 1-64 characters");
        return false;
    }
    for (char ch : value)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) {
            SetError(error, "checkpoint id contains invalid characters");
            return false;
        }
    return true;
}

std::string UtcNow() {
    const std::time_t value = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

bool ReadPath(const fs::path& path, SaveGameData& data, std::string* error) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            SetError(error, "save does not exist");
            return false;
        }
        nlohmann::json value;
        input >> value;
        return SaveGame::FromJson(std::move(value), data, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool ValidateSaveFile(const fs::path& path, std::string* error) {
    SaveGameData ignored;
    return ReadPath(path, ignored, error);
}
} // namespace

void SaveGame::SetStorageRoot(fs::path root) {
    std::lock_guard<std::mutex> lock(g_StorageMutex);
    g_StorageRoot = std::move(root);
}

void SaveGame::ClearStorageRootOverride() {
    std::lock_guard<std::mutex> lock(g_StorageMutex);
    g_StorageRoot.clear();
}

fs::path SaveGame::GetStorageRoot() {
    std::lock_guard<std::mutex> lock(g_StorageMutex);
    return g_StorageRoot.empty() ? AssetManager::Get().GetProjectRoot() / "Saved" / "SaveGames" : g_StorageRoot;
}

bool SaveGame::Resolve(const std::string& slot, std::string& path, std::string* error) {
    if (!IsValidSlot(slot, error))
        return false;
    const fs::path root = GetStorageRoot();
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        SetError(error, "failed to create save directory: " + ec.message());
        return false;
    }
    path = (root / slot).string();
    return true;
}

nlohmann::json SaveGame::ToJson(const SaveGameData& data) {
    return {{"version", SaveGameData::CurrentVersion},
            {"metadata",
             {{"displayName", data.metadata.displayName},
              {"scene", data.metadata.scene},
              {"savedAtUtc", data.metadata.savedAtUtc},
              {"playTimeSeconds", data.metadata.playTimeSeconds},
              {"screenshot", data.metadata.screenshot},
              {"buildId", data.metadata.buildId}}},
            {"checkpoint", data.checkpoint},
            {"player", data.player},
            {"collected", data.collected},
            {"settings", data.settings}};
}

bool SaveGame::FromJson(nlohmann::json value, SaveGameData& data, std::string* error) {
    if (!value.is_object()) {
        SetError(error, "save root must be an object");
        return false;
    }
    if (!value.contains("version"))
        value["version"] = 1;
    JsonMigrationRegistry migrations("save", SaveGameData::CurrentVersion);
    if (!migrations.Register(
            1,
            [](nlohmann::json& json, std::string*) {
                if (!json.contains("settings"))
                    json["settings"] = nlohmann::json::object();
                return true;
            },
            error) ||
        !migrations.Register(
            2,
            [](nlohmann::json& json, std::string*) {
                if (!json.contains("metadata"))
                    json["metadata"] = nlohmann::json::object();
                return true;
            },
            error) ||
        !migrations.Migrate(value, error))
        return false;
    const nlohmann::json metadata = value.value("metadata", nlohmann::json::object());
    if (!metadata.is_object()) {
        SetError(error, "save metadata must be an object");
        return false;
    }
    data.version = value.value("version", 0);
    data.metadata.displayName = metadata.value("displayName", std::string{});
    data.metadata.scene = metadata.value("scene", std::string{});
    data.metadata.savedAtUtc = metadata.value("savedAtUtc", std::string{});
    data.metadata.playTimeSeconds = metadata.value("playTimeSeconds", 0.0);
    data.metadata.screenshot = metadata.value("screenshot", std::string{});
    data.metadata.buildId = metadata.value("buildId", std::string{});
    data.checkpoint = value.value("checkpoint", std::string{});
    data.player = value.value("player", nlohmann::json::object());
    data.collected = value.value("collected", std::vector<std::string>{});
    data.settings = value.value("settings", nlohmann::json::object());
    if (!data.player.is_object() || !data.settings.is_object() || data.metadata.playTimeSeconds < 0.0) {
        SetError(error, "save player/settings or play time is invalid");
        return false;
    }
    return true;
}

bool SaveGame::Write(const std::string& slot, const SaveGameData& input, std::string* error) {
    std::string path;
    if (!Resolve(slot, path, error))
        return false;
    SaveGameData data = input;
    if (data.metadata.savedAtUtc.empty())
        data.metadata.savedAtUtc = UtcNow();
    if (data.metadata.buildId.empty())
        data.metadata.buildId = std::string(BuildInfo::BuildId);
    TransactionalWriteOptions options;
    options.keepBackup = true;
    options.validator = ValidateSaveFile;
    return TransactionalFileWriter::WriteText(path, ToJson(data).dump(2) + "\n", options, error);
}

bool SaveGame::Read(const std::string& slot, SaveGameData& data, std::string* error) {
    std::string path;
    return Resolve(slot, path, error) && ReadPath(path, data, error);
}

bool SaveGame::ReadBackup(const std::string& slot, SaveGameData& data, std::string* error) {
    std::string path;
    return Resolve(slot, path, error) && ReadPath(path + ".bak", data, error);
}

bool SaveGame::RestoreBackup(const std::string& slot, std::string* error) {
    std::string path;
    if (!Resolve(slot, path, error))
        return false;
    SaveGameData backup;
    if (!ReadPath(path + ".bak", backup, error))
        return false;
    TransactionalWriteOptions options;
    options.keepBackup = false;
    options.validator = ValidateSaveFile;
    return TransactionalFileWriter::WriteText(path, ToJson(backup).dump(2) + "\n", options, error);
}

std::vector<SaveGameSlotInfo> SaveGame::ListSlots() {
    std::vector<SaveGameSlotInfo> slots;
    const fs::path root = GetStorageRoot();
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec)
        return slots;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json")
            continue;
        SaveGameSlotInfo info;
        info.slot = entry.path().filename().string();
        SaveGameData data;
        info.valid = ReadPath(entry.path(), data, &info.error);
        info.metadata = data.metadata;
        info.hasBackup = fs::is_regular_file(entry.path().string() + ".bak", ec) && !ec;
        slots.push_back(std::move(info));
    }
    std::sort(slots.begin(), slots.end(), [](const auto& left, const auto& right) { return left.slot < right.slot; });
    return slots;
}

bool SaveGame::WriteAutosave(const SaveGameData& data, size_t slotCount, std::string* writtenSlot, std::string* error) {
    if (slotCount == 0 || slotCount > 16) {
        SetError(error, "autosave slot count must be between 1 and 16");
        return false;
    }
    const fs::path root = GetStorageRoot();
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        SetError(error, "failed to create save directory: " + ec.message());
        return false;
    }
    std::string selected;
    fs::file_time_type oldest = fs::file_time_type::max();
    for (size_t index = 0; index < slotCount; ++index) {
        const std::string slot = "autosave_" + std::to_string(index) + ".json";
        const fs::path path = root / slot;
        if (!fs::is_regular_file(path, ec) || ec) {
            selected = slot;
            ec.clear();
            break;
        }
        const auto writeTime = fs::last_write_time(path, ec);
        if (!ec && writeTime < oldest) {
            oldest = writeTime;
            selected = slot;
        }
        ec.clear();
    }
    if (selected.empty()) {
        SetError(error, "failed to select an autosave slot");
        return false;
    }
    SaveGameData autosave = data;
    if (autosave.metadata.displayName.empty())
        autosave.metadata.displayName = "Autosave";
    autosave.metadata.savedAtUtc.clear();
    if (!Write(selected, autosave, error))
        return false;
    if (writtenSlot)
        *writtenSlot = selected;
    return true;
}

bool SaveGame::WriteCheckpoint(const std::string& checkpointId, const SaveGameData& data, std::string* writtenSlot,
                               std::string* error) {
    if (!IsValidCheckpointId(checkpointId, error))
        return false;
    const std::string slot = "checkpoint_" + checkpointId + ".json";
    SaveGameData checkpoint = data;
    checkpoint.checkpoint = checkpointId;
    if (checkpoint.metadata.displayName.empty())
        checkpoint.metadata.displayName = "Checkpoint " + checkpointId;
    checkpoint.metadata.savedAtUtc.clear();
    if (!Write(slot, checkpoint, error))
        return false;
    if (writtenSlot)
        *writtenSlot = slot;
    return true;
}

bool SaveGame::FindLatestAutosave(SaveGameSlotInfo& info) {
    bool found = false;
    fs::file_time_type latest = fs::file_time_type::min();
    const fs::path root = GetStorageRoot();
    for (const auto& candidate : ListSlots()) {
        if (!candidate.valid || candidate.slot.rfind("autosave_", 0) != 0)
            continue;
        std::error_code ec;
        const auto writeTime = fs::last_write_time(root / candidate.slot, ec);
        if (!ec && (!found || writeTime > latest)) {
            found = true;
            latest = writeTime;
            info = candidate;
        }
    }
    return found;
}

bool SaveGame::Exists(const std::string& slot) {
    std::string path;
    if (!Resolve(slot, path, nullptr))
        return false;
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

bool SaveGame::Remove(const std::string& slot, std::string* error) {
    std::string path;
    if (!Resolve(slot, path, error))
        return false;
    std::error_code ec;
    const bool removed = fs::remove(path, ec);
    if (ec) {
        SetError(error, ec.message());
        return false;
    }
    fs::remove(path + ".bak", ec);
    return removed;
}
