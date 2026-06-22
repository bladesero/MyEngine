#pragma once

#include "Editor/EditorCommand.h"

#include <nlohmann/json.hpp>

class Actor;
class EditorContext;

class EditorSceneTransaction {
public:
    void Begin(const char* name, std::string beforeJson, uint64_t selection);
    bool Commit(EditorContext& context);
    void Cancel();
    bool IsActive() const { return m_Active; }

private:
    std::string m_Name;
    std::string m_BeforeJson;
    uint64_t m_Selection = 0;
    bool m_Active = false;
};

class EditorUndoUtil {
public:
    // Legacy full-scene snapshot
    static std::unique_ptr<IEditorCommand> MakeSceneSnapshotCommand(
        const char* name, const std::string& beforeJson, const std::string& afterJson,
        uint64_t beforeSelection, uint64_t afterSelection);
    static std::unique_ptr<IEditorCommand> MakeTransformCommand(
        const Actor& actor, const Transform& before, const Transform& after);

    // Fine-grained commands (P0)
    static std::string SerializeActorSubtree(const Actor& root);
    static nlohmann::json SerializeComponentData(const Component& comp);

    static std::unique_ptr<IEditorCommand> MakeCreateActorCommand(
        const ActorCreateDesc& desc, uint64_t newID);
    static std::unique_ptr<IEditorCommand> MakeDestroyActorCommand(
        const Actor& actor);
    static std::unique_ptr<IEditorCommand> MakeSetParentCommand(
        const Actor& child, uint64_t beforeParentID, uint64_t afterParentID);
    static std::unique_ptr<IEditorCommand> MakeMoveActorCommand(
        const Actor& child, uint64_t beforeParentID, uint64_t beforeNextSiblingID,
        uint64_t afterParentID, uint64_t afterNextSiblingID);
    static std::unique_ptr<IEditorCommand> MakeSetActiveCommand(
        const Actor& actor, bool afterActive);
    static std::unique_ptr<IEditorCommand> MakeSetNameCommand(
        const Actor& actor, const std::string& afterName);
    static std::unique_ptr<IEditorCommand> MakeAddComponentCommand(
        const Actor& actor, const std::string& typeName,
        const nlohmann::json& initialData = nlohmann::json::object());
    static std::unique_ptr<IEditorCommand> MakeRemoveComponentCommand(
        const Actor& actor, const Component& comp);
    static std::unique_ptr<IEditorCommand> MakeSetPropertyCommand(
        const Actor& actor, const std::string& componentType,
        const std::string& propertyName,
        const nlohmann::json& beforeJson, const nlohmann::json& afterJson);
};
