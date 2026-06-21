#include "Editor/EditorAssetRegistry.h"

#include <algorithm>
#include <cctype>

EditorAssetType EditorAssetRegistry::Classify(const std::filesystem::path& path) {
    std::string filename = path.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (filename.size() >= 12 && filename.compare(filename.size() - 12, 12, ".prefab.json") == 0)
        return EditorAssetType::Prefab;
    if (filename.size() >= 11 && filename.compare(filename.size() - 11, 11, ".scene.json") == 0)
        return EditorAssetType::Scene;
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
    return EditorAssetType::Unknown;
}
void EditorAssetRegistry::Refresh() {
    m_Assets.clear();
    std::error_code error;
    if (m_Root.empty() || !std::filesystem::is_directory(m_Root, error)) return;
    std::vector<std::filesystem::path> roots = {m_Root};
    const auto sourceRoot = m_Root.parent_path() / "SourceAssets";
    if (std::filesystem::is_directory(sourceRoot, error)) roots.push_back(sourceRoot);
    for (const auto& scanRoot : roots) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(scanRoot, error)) {
        if (error) break; if (!entry.is_regular_file()) continue;
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
    std::sort(m_Assets.begin(), m_Assets.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
}
bool EditorAssetRegistry::WatchForChanges() {
    std::vector<EditorAssetInfo> before = m_Assets; Refresh();
    if (before.size() != m_Assets.size()) return true;
    for (size_t index = 0; index < before.size(); ++index)
        if (before[index].relativePath != m_Assets[index].relativePath || before[index].modifiedTime != m_Assets[index].modifiedTime) return true;
    return false;
}
std::vector<EditorAssetInfo> EditorAssetRegistry::GetAssets(EditorAssetType filter) const {
    if (filter == EditorAssetType::Unknown) return m_Assets;
    std::vector<EditorAssetInfo> result;
    for (const auto& info : m_Assets) if (info.type == filter) result.push_back(info);
    return result;
}
const EditorAssetInfo* EditorAssetRegistry::GetAssetInfo(const std::string& path) const {
    for (const auto& info : m_Assets)
        if (info.relativePath == path || info.absolutePath.lexically_normal() == std::filesystem::path(path).lexically_normal()) return &info;
    return nullptr;
}
