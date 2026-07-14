#include "Assets/AssetDatabase.h"
#include "Core/TransactionalFileWriter.h"
#include "Project/JsonMigrationRegistry.h"

#include "Core/Sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace {
void SetError(std::string* error, std::string value) {
    if (error)
        *error = std::move(value);
}

const char* StateName(AssetImportState state) {
    switch (state) {
    case AssetImportState::Importing:
        return "importing";
    case AssetImportState::Failed:
        return "failed";
    case AssetImportState::Stale:
        return "stale";
    case AssetImportState::MissingSource:
        return "missing-source";
    default:
        return "ready";
    }
}

AssetImportState ParseState(const std::string& state) {
    if (state == "importing")
        return AssetImportState::Importing;
    if (state == "failed")
        return AssetImportState::Failed;
    if (state == "stale")
        return AssetImportState::Stale;
    if (state == "missing-source")
        return AssetImportState::MissingSource;
    return AssetImportState::Ready;
}

void AddIssue(std::vector<AssetDatabaseValidationIssue>& issues, AssetDatabaseValidationIssueCode code,
              std::string uuid, std::string path, std::string message) {
    issues.push_back({code, std::move(uuid), std::move(path), std::move(message)});
}

bool IsShaderSourceDependency(const AssetRecord& record, const std::string& dependency) {
    if (record.type != "shader")
        return false;
    std::string extension = std::filesystem::path(dependency).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".hlsl" || extension == ".hlsli";
}
} // namespace

std::string AssetDatabase::NormalizePath(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}

bool AssetDatabase::Open(std::filesystem::path databasePath, std::string* error) {
    m_Path = std::move(databasePath);
    m_Records.clear();
    m_LoadIssues.clear();
    if (!std::filesystem::exists(m_Path)) {
        RebuildIndexes();
        return true;
    }
    try {
        std::ifstream input(m_Path);
        nlohmann::json root;
        input >> root;
        JsonMigrationRegistry migrations("asset database", kVersion);
        if (!migrations.Migrate(root, error))
            return false;
        if (!root.contains("assets") || !root["assets"].is_array()) {
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
                record.diagnostics.push_back(
                    {item.value("severity", std::string{}), item.value("message", std::string{})});
            }
            if (!record.uuid.empty()) {
                auto [_, inserted] = m_Records.emplace(record.uuid, std::move(record));
                if (!inserted) {
                    AddIssue(m_LoadIssues, AssetDatabaseValidationIssueCode::DuplicateUuid,
                             value.value("uuid", std::string{}), {}, "asset database contains a duplicate uuid");
                }
            }
        }
        RebuildIndexes();
        return Validate(error);
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return false;
    }
}

bool AssetDatabase::Save(std::string* error) const {
    if (m_Path.empty()) {
        SetError(error, "asset database path is empty");
        return false;
    }
    try {
        nlohmann::json assets = nlohmann::json::array();
        auto records = GetAll();
        for (const AssetRecord& record : records) {
            nlohmann::json diagnostics = nlohmann::json::array();
            for (const auto& item : record.diagnostics)
                diagnostics.push_back({{"severity", item.severity}, {"message", item.message}});
            assets.push_back({{"uuid", record.uuid},
                              {"sourcePath", record.sourcePath},
                              {"artifactPath", record.artifactPath},
                              {"type", record.type},
                              {"importer", record.importer},
                              {"importerVersion", record.importerVersion},
                              {"sourceHash", record.sourceHash},
                              {"artifactHash", record.artifactHash},
                              {"settings", record.settingsJson},
                              {"dependencies", record.dependencies},
                              {"state", StateName(record.state)},
                              {"diagnostics", diagnostics},
                              {"alwaysCook", record.alwaysCook}});
        }
        const nlohmann::json root = {{"version", kVersion}, {"assets", assets}};
        TransactionalWriteOptions options;
        options.validator = [](const std::filesystem::path& candidate, std::string* validationError) {
            AssetDatabase ignored;
            return ignored.Open(candidate, validationError);
        };
        return TransactionalFileWriter::WriteText(m_Path, root.dump(2) + "\n", options, error);
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
    if (!m_Records.erase(uuid))
        return false;
    RebuildIndexes();
    return true;
}

void AssetDatabase::Clear() {
    m_Records.clear();
    RebuildIndexes();
}

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
            if (const AssetRecord* item = FindByUuid(dependency))
                result.push_back(item);
    return result;
}

std::vector<const AssetRecord*> AssetDatabase::GetReferencers(const std::string& uuid) const {
    std::vector<const AssetRecord*> result;
    const auto found = m_Referencers.find(uuid);
    if (found != m_Referencers.end())
        for (const auto& referencer : found->second)
            if (const AssetRecord* item = FindByUuid(referencer))
                result.push_back(item);
    return result;
}

std::vector<AssetRecord> AssetDatabase::GetAll() const {
    std::vector<AssetRecord> result;
    for (const auto& [uuid, record] : m_Records)
        result.push_back(record);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.uuid < right.uuid; });
    return result;
}

bool AssetDatabase::Validate(std::string* error) const {
    if (!m_LoadIssues.empty()) {
        SetError(error, m_LoadIssues.front().message);
        return false;
    }
    for (const auto& [uuid, record] : m_Records) {
        if (uuid.empty() || record.sourcePath.empty()) {
            SetError(error, "asset database contains incomplete record");
            return false;
        }
        for (const auto& dependency : record.dependencies) {
            if (dependency == uuid) {
                SetError(error, "asset directly depends on itself: " + uuid);
                return false;
            }
        }
    }
    return true;
}

std::string AssetDatabaseValidationReport::Summary() const {
    if (issues.empty())
        return "asset database validation passed";
    return "asset database validation failed (" + std::to_string(issues.size()) +
           " issues): " + issues.front().message +
           (issues.front().path.empty() ? std::string{} : " [" + issues.front().path + "]");
}

bool AssetDatabase::ValidateAgainstProject(const std::filesystem::path& projectRoot,
                                           AssetDatabaseValidationReport& report) const {
    report = {};
    report.issues.insert(report.issues.end(), m_LoadIssues.begin(), m_LoadIssues.end());
    std::unordered_map<std::string, std::string> sourceOwners;

    for (const auto& [uuid, record] : m_Records) {
        if (uuid.empty() || record.uuid.empty() || record.sourcePath.empty()) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::IncompleteRecord, uuid, record.sourcePath,
                     "asset record requires uuid and source path");
            continue;
        }
        if (uuid != record.uuid) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::DuplicateUuid, record.uuid, record.sourcePath,
                     "asset record uuid key mismatch");
        }
        const std::string source = NormalizePath(record.sourcePath);
        auto [_, inserted] = sourceOwners.emplace(source, uuid);
        if (!inserted) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::DuplicateSourcePath, uuid, source,
                     "source path is owned by multiple assets");
        }
        if (!std::filesystem::is_regular_file(record.sourcePath)) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::MissingSource, uuid, record.sourcePath,
                     "asset source is missing");
        }
        if (record.state != AssetImportState::Ready) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::StateNotReady, uuid, record.sourcePath,
                     "asset import state is not ready");
        }
        if (!record.artifactPath.empty()) {
            if (!std::filesystem::is_regular_file(record.artifactPath)) {
                AddIssue(report.issues, AssetDatabaseValidationIssueCode::MissingArtifact, uuid, record.artifactPath,
                         "asset artifact is missing");
            } else if (!record.artifactHash.empty()) {
                std::string hashError;
                const std::string hash = Sha256::HashFile(record.artifactPath, &hashError);
                if (!hashError.empty() || hash != record.artifactHash) {
                    AddIssue(report.issues, AssetDatabaseValidationIssueCode::ArtifactHashMismatch, uuid,
                             record.artifactPath, hashError.empty() ? "asset artifact hash mismatch" : hashError);
                }
            }
        }
        for (const auto& dependency : record.dependencies) {
            if (IsShaderSourceDependency(record, dependency))
                continue;
            if (!dependency.empty() && !FindByUuid(dependency)) {
                AddIssue(report.issues, AssetDatabaseValidationIssueCode::UnknownDependency, uuid, dependency,
                         "asset depends on an unknown uuid");
            }
        }
    }

    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::function<void(const std::string&, std::vector<std::string>&)> visit = [&](const std::string& uuid,
                                                                                   std::vector<std::string>& stack) {
        if (visited.count(uuid))
            return;
        if (!visiting.insert(uuid).second) {
            AddIssue(report.issues, AssetDatabaseValidationIssueCode::DependencyCycle, uuid, {},
                     "asset dependency cycle: " + uuid);
            return;
        }
        stack.push_back(uuid);
        if (const AssetRecord* record = FindByUuid(uuid)) {
            for (const auto& dependency : record->dependencies) {
                if (FindByUuid(dependency))
                    visit(dependency, stack);
            }
        }
        stack.pop_back();
        visiting.erase(uuid);
        visited.insert(uuid);
    };
    for (const auto& [uuid, _] : m_Records) {
        std::vector<std::string> stack;
        visit(uuid, stack);
    }

    (void)projectRoot;
    return report.Passed();
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
