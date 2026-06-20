#include "Assets/AssetDatabase.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace {
void SetError(std::string* error, std::string value) {
    if (error) *error = std::move(value);
}

const char* StateName(AssetImportState state) {
    switch (state) {
        case AssetImportState::Importing: return "importing";
        case AssetImportState::Failed: return "failed";
        case AssetImportState::Stale: return "stale";
        case AssetImportState::MissingSource: return "missing-source";
        default: return "ready";
    }
}

AssetImportState ParseState(const std::string& state) {
    if (state == "importing") return AssetImportState::Importing;
    if (state == "failed") return AssetImportState::Failed;
    if (state == "stale") return AssetImportState::Stale;
    if (state == "missing-source") return AssetImportState::MissingSource;
    return AssetImportState::Ready;
}
}

std::string AssetDatabase::NormalizePath(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}

bool AssetDatabase::Open(std::filesystem::path databasePath, std::string* error) {
    m_Path = std::move(databasePath);
    m_Records.clear();
    if (!std::filesystem::exists(m_Path)) {
        RebuildIndexes();
        return true;
    }
    try {
        std::ifstream input(m_Path);
        nlohmann::json root;
        input >> root;
        if (root.value("version", 0u) != kVersion || !root["assets"].is_array()) {
            SetError(error, "unsupported or malformed asset database");
            return false;
        }
        for (const auto& value : root["assets"]) {
            AssetRecord record;
            record.uuid = value.value("uuid", std::string{});
            record.sourcePath = NormalizePath(value.value("sourcePath", std::string{}));
            record.artifactPath = NormalizePath(value.value("artifactPath", std::string{}));
            record.type = value.value("type", std::string{});
            record.importer = value.value("importer", std::string{});
            record.importerVersion = value.value("importerVersion", 1u);
            record.sourceHash = value.value("sourceHash", std::string{});
            record.artifactHash = value.value("artifactHash", std::string{});
            record.settingsJson = value.value("settings", std::string("{}"));
            record.dependencies = value.value("dependencies", std::vector<std::string>{});
            record.state = ParseState(value.value("state", std::string("ready")));
            record.alwaysCook = value.value("alwaysCook", false);
            for (const auto& item : value.value("diagnostics", nlohmann::json::array())) {
                record.diagnostics.push_back({item.value("severity", std::string{}),
                                              item.value("message", std::string{})});
            }
            if (!record.uuid.empty()) m_Records.emplace(record.uuid, std::move(record));
        }
        RebuildIndexes();
        return Validate(error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool AssetDatabase::Save(std::string* error) const {
    if (m_Path.empty()) { SetError(error, "asset database path is empty"); return false; }
    try {
        nlohmann::json assets = nlohmann::json::array();
        auto records = GetAll();
        for (const AssetRecord& record : records) {
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& item : record.diagnostics)
                diagnostics.push_back({{"severity", item.severity}, {"message", item.message}});
            assets.push_back({
                {"uuid", record.uuid}, {"sourcePath", record.sourcePath},
                {"artifactPath", record.artifactPath}, {"type", record.type},
                {"importer", record.importer}, {"importerVersion", record.importerVersion},
                {"sourceHash", record.sourceHash}, {"artifactHash", record.artifactHash},
                {"settings", record.settingsJson}, {"dependencies", record.dependencies},
                {"state", StateName(record.state)}, {"diagnostics", diagnostics},
                {"alwaysCook", record.alwaysCook}
            });
        }
        const nlohmann::json root = {{"version", kVersion}, {"assets", assets}};
        std::filesystem::create_directories(m_Path.parent_path());
        const auto temporary = m_Path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) { SetError(error, "cannot create asset database temporary file"); return false; }
            output << root.dump(2);
            output.flush();
            if (!output) { SetError(error, "failed writing asset database"); return false; }
        }
        std::error_code ec;
        std::filesystem::remove(m_Path, ec);
        ec.clear();
        std::filesystem::rename(temporary, m_Path, ec);
        if (ec) { SetError(error, "failed installing asset database: " + ec.message()); return false; }
        return true;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool AssetDatabase::Upsert(AssetRecord record, std::string* error) {
    if (record.uuid.empty() || record.sourcePath.empty()) {
        SetError(error, "asset record requires uuid and source path");
        return false;
    }
    record.sourcePath = NormalizePath(record.sourcePath);
    record.artifactPath = NormalizePath(record.artifactPath);
    const auto source = m_SourceToUuid.find(record.sourcePath);
    if (source != m_SourceToUuid.end() && source->second != record.uuid) {
        SetError(error, "source path is already owned by another asset");
        return false;
    }
    m_Records[record.uuid] = std::move(record);
    RebuildIndexes();
    return true;
}

bool AssetDatabase::Remove(const std::string& uuid) {
    if (!m_Records.erase(uuid)) return false;
    RebuildIndexes();
    return true;
}

void AssetDatabase::Clear() { m_Records.clear(); RebuildIndexes(); }

const AssetRecord* AssetDatabase::FindByUuid(const std::string& uuid) const {
    const auto found = m_Records.find(uuid);
    return found == m_Records.end() ? nullptr : &found->second;
}

const AssetRecord* AssetDatabase::FindBySourcePath(const std::string& sourcePath) const {
    const auto found = m_SourceToUuid.find(NormalizePath(sourcePath));
    return found == m_SourceToUuid.end() ? nullptr : FindByUuid(found->second);
}

std::vector<const AssetRecord*> AssetDatabase::GetDependencies(const std::string& uuid) const {
    std::vector<const AssetRecord*> result;
    if (const AssetRecord* record = FindByUuid(uuid))
        for (const auto& dependency : record->dependencies)
            if (const AssetRecord* item = FindByUuid(dependency)) result.push_back(item);
    return result;
}

std::vector<const AssetRecord*> AssetDatabase::GetReferencers(const std::string& uuid) const {
    std::vector<const AssetRecord*> result;
    const auto found = m_Referencers.find(uuid);
    if (found != m_Referencers.end())
        for (const auto& referencer : found->second)
            if (const AssetRecord* item = FindByUuid(referencer)) result.push_back(item);
    return result;
}

std::vector<AssetRecord> AssetDatabase::GetAll() const {
    std::vector<AssetRecord> result;
    for (const auto& [uuid, record] : m_Records) result.push_back(record);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.uuid < right.uuid;
    });
    return result;
}

bool AssetDatabase::Validate(std::string* error) const {
    for (const auto& [uuid, record] : m_Records) {
        if (uuid.empty() || record.sourcePath.empty()) {
            SetError(error, "asset database contains incomplete record"); return false;
        }
        for (const auto& dependency : record.dependencies) {
            if (dependency == uuid) { SetError(error, "asset directly depends on itself: " + uuid); return false; }
        }
    }
    return true;
}

void AssetDatabase::RebuildIndexes() {
    m_SourceToUuid.clear();
    m_Referencers.clear();
    for (const auto& [uuid, record] : m_Records) {
        m_SourceToUuid[NormalizePath(record.sourcePath)] = uuid;
        for (const auto& dependency : record.dependencies)
            m_Referencers[dependency].push_back(uuid);
    }
}
