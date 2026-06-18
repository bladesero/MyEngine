#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class EditorAssetType { Unknown, Model, Texture, Material, Scene, Script, Shader };
struct EditorAssetInfo {
    std::filesystem::path absolutePath;
    std::string relativePath;
    EditorAssetType type = EditorAssetType::Unknown;
    std::filesystem::file_time_type modifiedTime{};
};

class EditorAssetRegistry {
public:
    void SetRoot(std::filesystem::path root) { m_Root = std::move(root); }
    void Refresh();
    bool WatchForChanges();
    std::vector<EditorAssetInfo> GetAssets(EditorAssetType filter = EditorAssetType::Unknown) const;
    const EditorAssetInfo* GetAssetInfo(const std::string& path) const;
    const std::filesystem::path& GetRoot() const { return m_Root; }
private:
    static EditorAssetType Classify(const std::filesystem::path& path);
    std::filesystem::path m_Root;
    std::vector<EditorAssetInfo> m_Assets;
};
