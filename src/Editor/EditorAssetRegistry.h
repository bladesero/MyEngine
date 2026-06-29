#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Assets/AssetDatabase.h"

enum class EditorAssetType { Unknown, Model, Texture, Material, Scene, Prefab, Script, Shader, Audio, UI };
struct EditorAssetInfo {
    std::filesystem::path absolutePath;
    std::string relativePath;
    EditorAssetType type = EditorAssetType::Unknown;
    std::filesystem::file_time_type modifiedTime{};
    std::string uuid;
    std::filesystem::path artifactPath;
    AssetImportState importState = AssetImportState::Ready;
    std::vector<AssetDiagnostic> diagnostics;
    bool imported = false;
};

struct EditorAssetFolderInfo {
    std::string relativePath;
    std::string displayName;
    size_t directAssetCount = 0;
    size_t assetCount = 0;
};

class EditorAssetRegistry {
public:
    static EditorAssetType Classify(const std::filesystem::path& path);
    void SetRoot(std::filesystem::path root) { m_Root = std::move(root); }
    void Refresh();
    bool WatchForChanges();
    std::vector<EditorAssetInfo> GetAssets(EditorAssetType filter = EditorAssetType::Unknown) const;
    const std::vector<EditorAssetFolderInfo>& GetFolders() const { return m_Folders; }
    std::vector<EditorAssetInfo> GetAssetsInFolder(const std::string& folderPath,
                                                   bool recursive = true,
                                                   EditorAssetType filter = EditorAssetType::Unknown) const;
    const EditorAssetInfo* GetAssetInfo(const std::string& path) const;
    const std::filesystem::path& GetRoot() const { return m_Root; }
private:
    std::filesystem::path m_Root;
    std::vector<EditorAssetInfo> m_Assets;
    std::vector<EditorAssetFolderInfo> m_Folders;
};
