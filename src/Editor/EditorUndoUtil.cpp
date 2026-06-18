#include "Editor/EditorUndoUtil.h"

#include "Editor/EditorContext.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

void EditorSceneTransaction::Begin(const char* name, std::string beforeJson,
                                   uint64_t selection)
{
    if (m_Active) return;
    m_Name = name ? name : "Scene Edit";
    m_BeforeJson = std::move(beforeJson);
    m_Selection = selection;
    m_Active = true;
}

bool EditorSceneTransaction::Commit(EditorContext& context)
{
    if (!m_Active || !context.GetScene() || !context.GetCommandStack()) return false;

    const std::string afterJson = SceneSerializer::SaveToString(*context.GetScene());
    if (afterJson == m_BeforeJson) {
        Cancel();
        return false;
    }

    SceneSerializer::LoadFromString(*context.GetScene(), m_BeforeJson);
    auto command = EditorUndoUtil::MakeSceneSnapshotCommand(
        m_Name.c_str(), m_BeforeJson, afterJson, m_Selection, m_Selection);
    Cancel();
    return context.GetCommandStack()->ExecuteCommand(std::move(command), context);
}

void EditorSceneTransaction::Cancel()
{
    m_Name.clear();
    m_BeforeJson.clear();
    m_Selection = 0;
    m_Active = false;
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSceneSnapshotCommand(
    const char* name, const std::string& beforeJson, const std::string& afterJson,
    uint64_t beforeSelection, uint64_t afterSelection) {
    return std::make_unique<SceneSnapshotCommand>(name ? name : "Scene Edit", beforeJson,
        afterJson, beforeSelection, afterSelection);
}
std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeTransformCommand(
    const Actor& actor, const Transform& before, const Transform& after) {
    return std::make_unique<SetActorTransformCommand>(actor.GetID(), before, after);
}
