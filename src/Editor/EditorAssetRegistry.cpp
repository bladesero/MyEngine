#include "Editor/EditorAssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <unordered_map>

namespace {
std::string NormalizeKey(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

std::string NormalizeFolder(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (!value.empty() && value.back() == '/') value.pop_back();
    if (value.empty() || value == ".") return "Content";
    return std::filesystem::path(value).lexically_normal().generic_string();
}

bool StartsWithPath(const std::string& value, const std::string& prefix) {
    return value == prefix ||
        (value.size() > prefix.size() && value.compare(0, prefix.size(), prefix) == 0 &&
         value[prefix.size()] == '/');
}

std::string DisplayNameForFolder(const std::string& path) {
    if (path == "Content" || path == "SourceAssets") return path;
    return std::filesystem::path(path).filename().generic_string();
}

std::string FolderForAsset(const EditorAssetInfo& asset) {
    std::filesystem::path relative = asset.relativePath;
    if (StartsWithPath(asset.relativePath, "SourceAssets")) {
        std::filesystem::path parent = relative.parent_path();
        return parent.empty() ? std::string("SourceAssets") : parent.generic_string();
    }
    std::filesystem::path parent = relative.parent_path();
    return parent.empty()
        ? std::string("Content")
        : (std::filesystem::path("Content") / parent).generic_string();
}

bool AssetInFolder(const EditorAssetInfo& asset, const std::string& folder, bool recursive) {
    const std::string assetFolder = FolderForAsset(asset);
    if (recursive) return StartsWithPath(assetFolder, folder);
    return assetFolder == folder;
}

void AddFolder(std::map<std::string, EditorAssetFolderInfo>& folders,
               const std::string& folder) {
    const std::string normalized = NormalizeFolder(folder);
    if (normalized.empty()) return;
    std::filesystem::path cursor = normalized;
    std::vector<std::string> chain;
    while (!cursor.empty() && cursor != cursor.root_path()) {
        chain.push_back(cursor.generic_string());
        if (cursor == "Content" || cursor == "SourceAssets") break;
        cursor = cursor.parent_path();
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        auto& info = folders[*it];
        info.relativePath = *it;
        info.displayName = DisplayNameForFolder(*it);
    }
}

EditorAssetType TypeFromRecord(const AssetRecord& record,
                               const std::filesystem::path& sourcePath) {
    if (record.type == "model") return EditorAssetType::Model;
    if (record.type == "texture") return EditorAssetType::Texture;
    if (record.type == "material") return EditorAssetType::Material;
    if (record.type == "shader") return EditorAssetType::Shader;
    return EditorAssetRegistry::Classify(sourcePath);
}
}

EditorAssetType EditorAssetRegistry::Classify(const std::filesystem::path& path) {
    std::string filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (filename.size() >= 12 && filename.compare(filename.size() - 12, 12, ".prefab.json") == 0)
        return EditorAssetType::Prefab;
    if (filename.size() >= 11 && filename.compare(filename.size() - 11, 11, ".scene.json") == 0)
        return EditorAssetType::Scene;
    if (filename.size() >= 8 && filename.compare(filename.size() - 8, 8, ".ui.json") == 0)
        return EditorAssetType::UI;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".obj" || extension == ".gltf" || extension == ".glb") return EditorAssetType::Model;
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".tga" || extension == ".hdr") return EditorAssetType::Texture;
    if (extension == ".mat") return EditorAssetType::Material;
    if (extension == ".json") return EditorAssetType::Scene;
    if (extension == ".lua" || extension == ".as") return EditorAssetType::Script;
    if (extension == ".shader" || extension == ".hlsl" || extension == ".hlsli") return EditorAssetType::Shader;
    if (extension == ".wav" || extension == ".ogg" || extension == ".flac" || extension == ".mp3")
        return EditorAssetType::Audio;
    if (extension == ".rml" || extension == ".rcss" || extension == ".ttf" ||
        extension == ".otf" || extension == ".ui.json")
        return EditorAssetType::UI;
    return EditorAssetType::Unknown;
}
void EditorAssetRegistry::Refresh() {
    m_Assets.clear();
    m_Folders.clear();
    std::error_code error;
    if (m_Root.empty() || !std::filesystem::is_directory(m_Root, error)) return;
    std::map<std::string, EditorAssetFolderInfo> folders;
    AddFolder(folders, "Content");
    AddFolder(folders, "SourceAssets");
    std::vector<std::filesystem::path> roots = {m_Root};
    const auto sourceRoot = m_Root.parent_path() / "SourceAssets";
    if (std::filesystem::is_directory(sourceRoot, error)) roots.push_back(sourceRoot);
    for (const auto& scanRoot : roots) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(scanRoot, error)) {
        if (error) break;
        if (entry.is_directory(error)) {
            const auto relative = std::filesystem::relative(entry.path(), scanRoot, error);
            if (!error) {
                AddFolder(folders, scanRoot == m_Root
                    ? (std::filesystem::path("Content") / relative).generic_string()
                    : (std::filesystem::path("SourceAssets") / relative).generic_string());
            }
            continue;
        }
        if (!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == ".meta") continue;
        EditorAssetInfo info; info.absolutePath = entry.path();
        info.relativePath = scanRoot == m_Root
            ? std::filesystem::relative(entry.path(), m_Root, error).generic_string()
            : (std::filesystem::path("SourceAssets") /
               std::filesystem::relative(entry.path(), scanRoot, error)).generic_string();
        info.type = Classify(entry.path()); info.modifiedTime = entry.last_write_time(error);
        m_Assets.push_back(std::move(info));
    }
    }
    std::unordered_map<std::string, size_t> byPath;
    for (size_t i = 0; i < m_Assets.size(); ++i) {
        byPath[NormalizeKey(m_Assets[i].absolutePath)] = i;
    }
    const auto databasePath = m_Root.parent_path() / ".myengine" / "AssetDatabase.json";
    AssetDatabase database;
    if (database.Open(databasePath)) {
        for (const AssetRecord& record : database.GetAll()) {
            const std::filesystem::path sourcePath = record.sourcePath;
            const std::string key = NormalizeKey(sourcePath);
            auto found = byPath.find(key);
            EditorAssetInfo* info = nullptr;
            if (found == byPath.end()) {
                EditorAssetInfo item;
                item.absolutePath = sourcePath;
                item.relativePath = (std::filesystem::path("SourceAssets") /
                    std::filesystem::relative(sourcePath, m_Root.parent_path() / "SourceAssets", error)).generic_string();
                item.type = TypeFromRecord(record, sourcePath);
                item.modifiedTime = std::filesystem::is_regular_file(sourcePath, error)
                    ? std::filesystem::last_write_time(sourcePath, error)
                    : std::filesystem::file_time_type{};
                m_Assets.push_back(std::move(item));
                byPath[key] = m_Assets.size() - 1;
                info = &m_Assets.back();
            } else {
                info = &m_Assets[found->second];
            }
            info->uuid = record.uuid;
            info->artifactPath = record.artifactPath;
            info->importState = record.state;
            info->diagnostics = record.diagnostics;
            info->imported = true;
            info->type = TypeFromRecord(record, sourcePath);
        }
    }
    std::sort(m_Assets.begin(), m_Assets.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
    for (const auto& asset : m_Assets) {
        const std::string directFolder = FolderForAsset(asset);
        AddFolder(folders, directFolder);
        folders[directFolder].directAssetCount += 1;
        for (auto& [folderPath, folder] : folders) {
            if (StartsWithPath(directFolder, folderPath)) folder.assetCount += 1;
        }
    }
    m_Folders.reserve(folders.size());
    for (auto& [path, folder] : folders) m_Folders.push_back(std::move(folder));
    std::sort(m_Folders.begin(), m_Folders.end(),
              [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
}
bool EditorAssetRegistry::WatchForChanges() {
    std::vector<EditorAssetInfo> before = m_Assets;
    std::vector<EditorAssetFolderInfo> beforeFolders = m_Folders;
    Refresh();
    if (before.size() != m_Assets.size()) return true;
    for (size_t index = 0; index < before.size(); ++index)
        if (before[index].relativePath != m_Assets[index].relativePath || before[index].modifiedTime != m_Assets[index].modifiedTime) return true;
    if (beforeFolders.size() != m_Folders.size()) return true;
    for (size_t index = 0; index < beforeFolders.size(); ++index)
        if (beforeFolders[index].relativePath != m_Folders[index].relativePath ||
            beforeFolders[index].assetCount != m_Folders[index].assetCount ||
            beforeFolders[index].directAssetCount != m_Folders[index].directAssetCount) return true;
    return false;
}
std::vector<EditorAssetInfo> EditorAssetRegistry::GetAssets(EditorAssetType filter) const {
    if (filter == EditorAssetType::Unknown) return m_Assets;
    std::vector<EditorAssetInfo> result;
    for (const auto& info : m_Assets) if (info.type == filter) result.push_back(info);
    return result;
}
std::vector<EditorAssetInfo> EditorAssetRegistry::GetAssetsInFolder(
    const std::string& folderPath, bool recursive, EditorAssetType filter) const {
    const std::string folder = NormalizeFolder(folderPath);
    std::vector<EditorAssetInfo> result;
    for (const auto& info : m_Assets) {
        if (filter != EditorAssetType::Unknown && info.type != filter) continue;
        if (AssetInFolder(info, folder, recursive)) result.push_back(info);
    }
    return result;
}
const EditorAssetInfo* EditorAssetRegistry::GetAssetInfo(const std::string& path) const {
    for (const auto& info : m_Assets)
        if (info.relativePath == path || info.absolutePath.lexically_normal() == std::filesystem::path(path).lexically_normal()) return &info;
    return nullptr;
}
