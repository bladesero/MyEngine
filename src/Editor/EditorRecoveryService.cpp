#include "Editor/EditorRecoveryService.h"

#include "Core/TransactionalFileWriter.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;
namespace {
constexpr uint32_t kRecoveryVersion = 1;

void SetError(std::string* error, const std::string& message) { if (error) *error = message; }

std::string SnapshotId(const std::string& scenePath, uint64_t revision)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0')
           << std::hash<std::string>{}(scenePath) << '-' << std::dec << revision;
    return stream.str();
}

bool ReadJson(const fs::path& path, nlohmann::json& value, std::string* error)
{
    try {
        std::ifstream input(path);
        if (!input) { SetError(error, "failed to open recovery file: " + path.string()); return false; }
        input >> value;
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}
}

bool EditorRecoveryService::OpenProject(const fs::path& projectRoot, std::string* error)
{
    if (error) error->clear();
    m_RecoveryRoot = projectRoot / ".myengine" / "recovery";
    std::error_code ec;
    fs::create_directories(m_RecoveryRoot, ec);
    if (ec) { SetError(error, "failed to create recovery directory: " + ec.message()); return false; }
    const fs::path statePath = m_RecoveryRoot / "session.json";
    m_PreviousShutdownClean = true;
    if (fs::is_regular_file(statePath, ec) && !ec) {
        nlohmann::json state;
        if (!ReadJson(statePath, state, error)) return false;
        m_PreviousShutdownClean = state.value("clean", false);
    }
    return WriteSessionState(false, error);
}

bool EditorRecoveryService::WriteSessionState(bool clean, std::string* error)
{
    if (m_RecoveryRoot.empty()) { SetError(error, "recovery project is not open"); return false; }
    const nlohmann::json state = {{"version", kRecoveryVersion}, {"clean", clean}};
    return TransactionalFileWriter::WriteText(m_RecoveryRoot / "session.json",
                                               state.dump(2) + "\n", {}, error);
}

bool EditorRecoveryService::WriteSnapshot(const std::string& scenePath, uint64_t revision,
                                          const std::string& serializedScene, std::string* error)
{
    if (m_RecoveryRoot.empty() || scenePath.empty()) {
        SetError(error, "recovery project and scene path are required"); return false;
    }
    try {
        const std::string id = SnapshotId(scenePath, revision);
        const nlohmann::json envelope = {{"version", kRecoveryVersion}, {"id", id},
            {"scenePath", scenePath}, {"revision", revision},
            {"scene", nlohmann::json::parse(serializedScene)}};
        TransactionalWriteOptions options;
        options.validator = [](const fs::path& candidate, std::string* validationError) {
            nlohmann::json value;
            return ReadJson(candidate, value, validationError) && value.value("version", 0u) == kRecoveryVersion &&
                   value.contains("scene") && value["scene"].is_object();
        };
        return TransactionalFileWriter::WriteText(m_RecoveryRoot / (id + ".recovery.json"),
                                                   envelope.dump(2) + "\n", options, error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what()); return false;
    }
}

std::vector<EditorRecoverySnapshot> EditorRecoveryService::ListSnapshots(std::string* error) const
{
    std::vector<EditorRecoverySnapshot> snapshots;
    if (error) error->clear();
    std::error_code ec;
    for (fs::directory_iterator it(m_RecoveryRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".json" ||
            it->path().filename() == "session.json") continue;
        nlohmann::json value;
        std::string ignored;
        if (!ReadJson(it->path(), value, &ignored) || value.value("version", 0u) != kRecoveryVersion) continue;
        snapshots.push_back({value.value("id", std::string{}), value.value("scenePath", std::string{}),
                             value.value("revision", uint64_t{0}), it->path()});
    }
    if (ec) SetError(error, "failed to enumerate recovery snapshots: " + ec.message());
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        return left.revision > right.revision ||
               (left.revision == right.revision && left.id < right.id);
    });
    return snapshots;
}

bool EditorRecoveryService::ReadSnapshot(const EditorRecoverySnapshot& snapshot,
                                         std::string& serializedScene, std::string* error) const
{
    nlohmann::json value;
    if (!ReadJson(snapshot.file, value, error) || !value.contains("scene") || !value["scene"].is_object()) {
        if (error && error->empty()) SetError(error, "recovery snapshot has no scene payload");
        return false;
    }
    serializedScene = value["scene"].dump(4);
    return true;
}

bool EditorRecoveryService::RemoveSnapshot(const std::string& id, std::string* error)
{
    std::error_code ec;
    fs::remove(m_RecoveryRoot / (id + ".recovery.json"), ec);
    if (ec) { SetError(error, "failed to remove recovery snapshot: " + ec.message()); return false; }
    return true;
}

bool EditorRecoveryService::RemoveMatchingRevision(const std::string& scenePath, uint64_t revision,
                                                   std::string* error)
{
    return RemoveSnapshot(SnapshotId(scenePath, revision), error);
}

bool EditorRecoveryService::MarkCleanShutdown(std::string* error)
{
    return WriteSessionState(true, error);
}
