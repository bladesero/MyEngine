#include "Editor/EditorResourceOperator.h"

#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Assets/MaterialAsset.h"
#include "Scene/SceneSerializer.h"
#include "Scene/Scene.h"

// ==========================================================================
// AssetModifier
// ==========================================================================

bool AssetModifier::Modify(EditorContext& context)
{
    // 1. Read current file content (before snapshot)
    std::string beforeContent;
    {
        std::ifstream in(m_AssetPath, std::ios::binary);
        if (!in) {
            Logger::Warn("[AssetModifier] Cannot read asset before edit: ", m_AssetPath);
            return false;
        }
        beforeContent = std::string(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
    }

    // 2. Load the asset
    auto handle = AssetManager::Get().GetByPath<Asset>(m_AssetPath);
    if (!handle.IsValid()) {
        handle = AssetManager::Get().Load<Asset>(m_AssetPath);
    }
    if (!handle.IsValid()) {
        Logger::Warn("[AssetModifier] Cannot load asset: ", m_AssetPath);
        return false;
    }

    // 3. Apply in-memory modification
    if (!ApplyTo(*handle.Get())) {
        Logger::Warn("[AssetModifier] ApplyTo returned false: ", m_AssetPath);
        return false;
    }

    // 4. Read after content
    std::string afterContent;
    {
        std::ifstream in(m_AssetPath, std::ios::binary);
        if (!in) {
            Logger::Warn("[AssetModifier] Cannot read asset after edit: ", m_AssetPath);
            return false;
        }
        afterContent = std::string(std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>());
    }

    // If ApplyTo already saved the file, afterContent should differ.
    // If not, we need to re-read to detect no-change.
    if (beforeContent == afterContent) {
        return false; // No actual change
    }

    // 5. Push undo command
    if (auto* stack = context.GetCommandStack()) {
        if (!stack->ExecuteCommand(
                std::make_unique<ModifyAssetCommand>(m_AssetPath, beforeContent, afterContent),
                context)) {
            std::ofstream restore(m_AssetPath, std::ios::binary | std::ios::trunc);
            restore.write(beforeContent.data(),
                          static_cast<std::streamsize>(beforeContent.size()));
            restore.close();
            AssetManager::Get().Reload(m_AssetPath);
            if (auto* registry = context.GetAssetRegistry()) registry->Refresh();
            Logger::Warn("[AssetModifier] Failed to commit asset modification: ",
                         m_AssetPath);
            return false;
        }
    }

    // 6. Reload in AssetManager to update GPU resources
    AssetManager::Get().Reload(m_AssetPath);

    // 7. Refresh editor asset registry
    if (auto* registry = context.GetAssetRegistry()) {
        registry->Refresh();
    }

    return true;
}

// ==========================================================================
// MaterialModifier
// ==========================================================================

bool MaterialModifier::ApplyTo(Asset& asset)
{
    auto* mat = dynamic_cast<MaterialAsset*>(&asset);
    if (!mat) {
        Logger::Warn("[MaterialModifier] Asset is not a Material: ", m_AssetPath);
        return false;
    }

    // Snapshot before modification
    auto before = std::make_shared<MaterialAsset>(mat->GetPath());
    before->ReloadFrom(*mat);

    // Apply user modification
    if (!m_Fn(*mat)) return false;

    // Save to disk
    if (!SaveMaterialAssetToFile(*mat, m_AssetPath)) {
        // Restore from snapshot on save failure
        mat->ReloadFrom(*before);
        Logger::Warn("[MaterialModifier] Failed to save: ", m_AssetPath);
        return false;
    }

    return true;
}

// ==========================================================================
// SceneModifier
// ==========================================================================

bool SceneModifier::CanModify(EditorContext& context) const
{
    return context.GetScene() != nullptr;
}

bool SceneModifier::Modify(EditorContext& context)
{
    Scene* scene = context.GetScene();
    if (!scene || !context.IsEditing()) return false;

    const std::string beforeJson = SceneSerializer::SaveToString(*scene);
    const uint64_t selection = context.GetSelection().GetActorID();

    if (!ApplyTo(context)) return false;

    const std::string afterJson = SceneSerializer::SaveToString(*scene);
    if (beforeJson == afterJson) return false;

    SceneSerializer::LoadFromString(*scene, beforeJson);
    if (auto* stack = context.GetCommandStack()) {
        stack->ExecuteCommand(
            std::make_unique<SceneSnapshotCommand>(GetLabel(), beforeJson, afterJson,
                                                    selection, selection),
            context);
    }
    context.MarkSceneDirty();
    return true;
}
