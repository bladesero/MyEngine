#include "Editor/EditorCommand.h"

#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorProfiler.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetMeta.h"
#include "Scene/Actor.h"
#include "Scene/ActorSubtreeSerializer.h"
#include "Scene/Component.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"
#include "Core/Logger.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

void RefreshAssetRegistry(EditorContext& context) {
    if (EditorAssetRegistry* registry = context.GetAssetRegistry())
        registry->Refresh();
}

void RefreshModifiedAsset(EditorContext& context, const std::string& path) {
    if (!path.empty())
        AssetManager::Get().Reload(path);
    RefreshAssetRegistry(context);
}

void RetargetAssetSelection(EditorContext& context, const std::string& fromPath, const std::string& toPath) {
    if (!context.GetSelection().HasAsset())
        return;
    const std::filesystem::path selected =
        std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal();
    const std::filesystem::path from = std::filesystem::path(fromPath).lexically_normal();
    if (selected == from)
        context.GetSelection().SelectAssetPath(toPath);
}

std::filesystem::path CommandProjectRoot(const EditorContext& context) {
    if (!context.GetProjectRoot().empty())
        return context.GetProjectRoot();
    if (!context.GetContentRoot().empty())
        return context.GetContentRoot().parent_path();
    if (const EditorAssetRegistry* registry = context.GetAssetRegistry()) {
        const std::filesystem::path& root = registry->GetRoot();
        if (!root.empty())
            return root.parent_path();
    }
    return {};
}

std::filesystem::path CommandAbsolutePath(const std::filesystem::path& path, const std::filesystem::path& projectRoot) {
    std::error_code error;
    const std::filesystem::path value = path.is_absolute() || projectRoot.empty() ? path : projectRoot / path;
    std::filesystem::path absolute = std::filesystem::absolute(value, error);
    if (error)
        absolute = value;
    return absolute.lexically_normal();
}

bool PathUnderOrEqual(const std::filesystem::path& path, const std::filesystem::path& root,
                      std::filesystem::path& relative) {
    const std::filesystem::path candidate = path.lexically_normal();
    const std::filesystem::path base = root.lexically_normal();
    if (candidate == base) {
        relative.clear();
        return true;
    }
    relative = candidate.lexically_relative(base);
    if (relative.empty())
        return false;
    const auto first = *relative.begin();
    return first != ".." && first != ".";
}

std::string DatabasePathStringFor(const std::filesystem::path& replacement, bool wasRelative,
                                  const std::filesystem::path& projectRoot) {
    std::error_code error;
    if (wasRelative && !projectRoot.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(replacement, projectRoot, error);
        if (!error && !relative.empty())
            return relative.generic_string();
    }
    return replacement.lexically_normal().generic_string();
}

bool RewriteDatabasePath(std::string& value, const std::filesystem::path& oldPath, const std::filesystem::path& newPath,
                         const std::filesystem::path& projectRoot) {
    if (value.empty())
        return false;
    const std::filesystem::path original(value);
    const bool wasRelative = !original.is_absolute();
    const std::filesystem::path absolute = CommandAbsolutePath(original, projectRoot);
    const std::filesystem::path oldAbsolute = CommandAbsolutePath(oldPath, projectRoot);
    const std::filesystem::path newAbsolute = CommandAbsolutePath(newPath, projectRoot);

    std::filesystem::path relative;
    if (!PathUnderOrEqual(absolute, oldAbsolute, relative))
        return false;

    const std::filesystem::path replacement = relative.empty() ? newAbsolute : newAbsolute / relative;
    value = DatabasePathStringFor(replacement, wasRelative, projectRoot);
    return true;
}

bool RetargetAssetDatabasePaths(EditorContext& context, const std::string& oldPath, const std::string& newPath) {
    const std::filesystem::path projectRoot = CommandProjectRoot(context);
    if (projectRoot.empty())
        return true;
    const std::filesystem::path databasePath = projectRoot / ".myengine" / "AssetDatabase.json";
    if (!std::filesystem::exists(databasePath))
        return true;

    std::string error;
    AssetDatabase database;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for path retarget: ", error);
        return false;
    }

    bool changed = false;
    for (AssetRecord record : database.GetAll()) {
        bool recordChanged = false;
        recordChanged = RewriteDatabasePath(record.sourcePath, oldPath, newPath, projectRoot) || recordChanged;
        recordChanged = RewriteDatabasePath(record.artifactPath, oldPath, newPath, projectRoot) || recordChanged;
        if (!recordChanged)
            continue;
        if (!database.Upsert(std::move(record), &error)) {
            Logger::Warn("[EditorAsset] Failed to update asset database path: ", error);
            return false;
        }
        changed = true;
    }

    if (!changed)
        return true;
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database path retarget: ", error);
        return false;
    }
    return true;
}

bool AssetDatabaseRecordMatchesPath(const AssetRecord& record, const std::filesystem::path& targetPath,
                                    const std::filesystem::path& projectRoot) {
    auto matches = [&](const std::string& value) {
        if (value.empty())
            return false;
        const std::filesystem::path absolute = CommandAbsolutePath(std::filesystem::path(value), projectRoot);
        std::filesystem::path relative;
        return PathUnderOrEqual(absolute, CommandAbsolutePath(targetPath, projectRoot), relative);
    };
    return matches(record.sourcePath) || matches(record.artifactPath);
}

std::vector<AssetRecord> CaptureAssetDatabaseRecordsForPath(EditorContext& context, const std::string& assetPath) {
    const std::filesystem::path projectRoot = CommandProjectRoot(context);
    if (projectRoot.empty())
        return {};
    const std::filesystem::path databasePath = projectRoot / ".myengine" / "AssetDatabase.json";
    if (!std::filesystem::exists(databasePath))
        return {};

    std::string error;
    AssetDatabase database;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for delete snapshot: ", error);
        return {};
    }

    std::vector<AssetRecord> records;
    for (const AssetRecord& record : database.GetAll()) {
        if (AssetDatabaseRecordMatchesPath(record, assetPath, projectRoot)) {
            records.push_back(record);
        }
    }
    return records;
}

bool RemoveAssetDatabaseRecords(EditorContext& context, const std::vector<AssetRecord>& records) {
    if (records.empty())
        return true;
    const std::filesystem::path projectRoot = CommandProjectRoot(context);
    if (projectRoot.empty())
        return true;
    const std::filesystem::path databasePath = projectRoot / ".myengine" / "AssetDatabase.json";
    if (!std::filesystem::exists(databasePath))
        return true;

    std::string error;
    AssetDatabase database;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for delete: ", error);
        return false;
    }
    bool changed = false;
    for (const AssetRecord& record : records) {
        changed = database.Remove(record.uuid) || changed;
    }
    if (!changed)
        return true;
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after delete: ", error);
        return false;
    }
    return true;
}

bool RestoreAssetDatabaseRecords(EditorContext& context, const std::vector<AssetRecord>& records) {
    if (records.empty())
        return true;
    const std::filesystem::path projectRoot = CommandProjectRoot(context);
    if (projectRoot.empty())
        return true;
    const std::filesystem::path databasePath = projectRoot / ".myengine" / "AssetDatabase.json";

    std::string error;
    AssetDatabase database;
    if (!database.Open(databasePath, &error)) {
        Logger::Warn("[EditorAsset] Failed to open asset database for undo delete: ", error);
        return false;
    }
    for (AssetRecord record : records) {
        if (!database.Upsert(std::move(record), &error)) {
            Logger::Warn("[EditorAsset] Failed to restore asset database record: ", error);
            return false;
        }
    }
    if (!database.Save(&error)) {
        Logger::Warn("[EditorAsset] Failed to save asset database after undo delete: ", error);
        return false;
    }
    return true;
}

class CompositeEditorCommand final : public IEditorCommand {
public:
    CompositeEditorCommand(std::string name, std::vector<std::unique_ptr<IEditorCommand>> commands)
        : m_Name(std::move(name)), m_Commands(std::move(commands)) {}

    bool Execute(EditorContext& context) override {
        size_t executedCount = 0;
        for (auto& command : m_Commands) {
            if (!command->Execute(context)) {
                while (executedCount > 0)
                    m_Commands[--executedCount]->Undo(context);
                return false;
            }
            ++executedCount;
        }
        return true;
    }

    bool Undo(EditorContext& context) override {
        size_t undoneCount = 0;
        for (auto it = m_Commands.rbegin(); it != m_Commands.rend(); ++it) {
            if (!(*it)->Undo(context)) {
                auto restore = it.base();
                for (size_t index = 0; index < undoneCount; ++index) {
                    (*restore++)->Execute(context);
                }
                return false;
            }
            ++undoneCount;
        }
        return true;
    }

    const char* GetName() const override { return m_Name.c_str(); }

private:
    std::string m_Name;
    std::vector<std::unique_ptr<IEditorCommand>> m_Commands;
};

bool ShouldMarkSceneDirty(const IEditorCommand& command) {
    const auto* resourceCommand = dynamic_cast<const IResourceCommand*>(&command);
    return !resourceCommand || !resourceCommand->IsResourceCommand();
}

using CommandClock = std::chrono::steady_clock;

double ElapsedCommandMs(CommandClock::time_point start) {
    return std::chrono::duration<double, std::milli>(CommandClock::now() - start).count();
}

void RecordCommandEvent(EditorContext& context, const char* operation, const std::string& commandName,
                        double durationMs, bool success, bool markSceneDirty) {
    if (EditorProfiler* profiler = context.GetProfiler()) {
        std::string details = "command=" + commandName;
        details += success ? ";success=true" : ";success=false";
        details += markSceneDirty ? ";dirty=scene" : ";dirty=resource";
        profiler->RecordEvent("EditorCommand", operation, durationMs, std::move(details));
    }

    if (!success) {
        Logger::Warn("[EditorCommand] ", operation, " failed: ", commandName);
    }
}

// Helper: serialize a single actor''s subtree as a JSON array of descriptors.
// Each descriptor: {id, name, active, editorFlags, parentID_relative, transform, components[]}
// The root''s parentID_relative will be 0 (caller stores real parent separately).
nlohmann::json SerializeSubtreeActors(const Actor& root) {
    nlohmann::json arr = nlohmann::json::array();
    std::function<void(const Actor&)> collect = [&](const Actor& actor) {
        nlohmann::json a;
        a["id"] = actor.GetID();
        a["name"] = actor.GetName();
        a["active"] = actor.IsActiveSelf();
        a["editorFlags"] = actor.GetEditorFlags();
        a["parentID"] = actor.GetParent() ? actor.GetParent()->GetID() : uint64_t(0);

        nlohmann::json t;
        t["position"] = nlohmann::json::array(
            {actor.GetTransform().position.x, actor.GetTransform().position.y, actor.GetTransform().position.z});
        t["rotation"] = nlohmann::json::array(
            {actor.GetTransform().rotation.x, actor.GetTransform().rotation.y, actor.GetTransform().rotation.z});
        t["scale"] = nlohmann::json::array(
            {actor.GetTransform().scale.x, actor.GetTransform().scale.y, actor.GetTransform().scale.z});
        a["transform"] = t;

        nlohmann::json comps = nlohmann::json::array();
        actor.ForEachComponent([&](Component& comp) {
            nlohmann::json c;
            c["type"] = comp.GetTypeName();
            c["enabled"] = comp.IsEnabled();
            nlohmann::json data = nlohmann::json::object();
            comp.Serialize(data);
            c["data"] = data;
            comps.push_back(c);
        });
        a["components"] = comps;
        arr.push_back(a);

        for (const auto* child : actor.GetChildren())
            collect(*child);
    };
    collect(root);
    return arr;
}

// Helper: convert Vec3 from JSON array
Vec3 Vec3FromJsonArray(const nlohmann::json& j) {
    Vec3 out;
    if (!j.is_array() || j.size() < 3)
        return out;
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = j[2].get<float>();
    return out;
}

// Helper: find the next available persistent ID by scanning the scene
uint64_t FindNextID(const Scene& scene) {
    uint64_t maxID = 0;
    scene.ForEach([&](const Actor& actor) {
        if (actor.GetID() > maxID)
            maxID = actor.GetID();
    });
    return maxID + 1;
}

} // namespace

// ==========================================================================
// EditorCommandStack
// ==========================================================================

bool EditorCommandStack::ExecuteCommand(std::unique_ptr<IEditorCommand> command, EditorContext& context) {
    if (!command)
        return false;
    const bool markSceneDirty = ShouldMarkSceneDirty(*command);
    const std::string commandName = command->GetName() ? command->GetName() : "Command";
    const auto start = CommandClock::now();
    if (!command->Execute(context)) {
        RecordCommandEvent(context, "Execute", commandName, ElapsedCommandMs(start), false, markSceneDirty);
        return false;
    }
    RecordCommandEvent(context, "Execute", commandName, ElapsedCommandMs(start), true, markSceneDirty);
    if (m_InTransaction)
        m_TransactionCommands.push_back(std::move(command));
    else
        m_Undo.push_back(std::move(command));
    m_Redo.clear();
    if (markSceneDirty)
        context.MarkSceneDirty();
    return true;
}
bool EditorCommandStack::Undo(EditorContext& context) {
    if (m_InTransaction || m_Undo.empty())
        return false;
    auto command = std::move(m_Undo.back());
    m_Undo.pop_back();
    const bool markSceneDirty = ShouldMarkSceneDirty(*command);
    const std::string commandName = command->GetName() ? command->GetName() : "Command";
    const auto start = CommandClock::now();
    if (!command->Undo(context)) {
        RecordCommandEvent(context, "Undo", commandName, ElapsedCommandMs(start), false, markSceneDirty);
        m_Undo.push_back(std::move(command));
        return false;
    }
    RecordCommandEvent(context, "Undo", commandName, ElapsedCommandMs(start), true, markSceneDirty);
    m_Redo.push_back(std::move(command));
    if (markSceneDirty)
        context.MarkSceneDirty();
    return true;
}
bool EditorCommandStack::Redo(EditorContext& context) {
    if (m_InTransaction || m_Redo.empty())
        return false;
    auto command = std::move(m_Redo.back());
    m_Redo.pop_back();
    const bool markSceneDirty = ShouldMarkSceneDirty(*command);
    const std::string commandName = command->GetName() ? command->GetName() : "Command";
    const auto start = CommandClock::now();
    if (!command->Execute(context)) {
        RecordCommandEvent(context, "Redo", commandName, ElapsedCommandMs(start), false, markSceneDirty);
        m_Redo.push_back(std::move(command));
        return false;
    }
    RecordCommandEvent(context, "Redo", commandName, ElapsedCommandMs(start), true, markSceneDirty);
    m_Undo.push_back(std::move(command));
    if (markSceneDirty)
        context.MarkSceneDirty();
    return true;
}
void EditorCommandStack::Clear() {
    m_Undo.clear();
    m_Redo.clear();
    CancelTransaction();
}
bool EditorCommandStack::BeginTransaction(const std::string& name) {
    if (m_InTransaction)
        return false;
    m_TransactionName = name.empty() ? "Editor Transaction" : name;
    m_TransactionCommands.clear();
    m_InTransaction = true;
    return true;
}
bool EditorCommandStack::CommitTransaction() {
    if (!m_InTransaction)
        return false;
    m_InTransaction = false;
    if (m_TransactionCommands.empty()) {
        m_TransactionName.clear();
        return false;
    }
    if (m_TransactionCommands.size() == 1) {
        m_Undo.push_back(std::move(m_TransactionCommands.front()));
    } else {
        m_Undo.push_back(std::make_unique<CompositeEditorCommand>(m_TransactionName, std::move(m_TransactionCommands)));
    }
    m_TransactionCommands.clear();
    m_TransactionName.clear();
    return true;
}
void EditorCommandStack::CancelTransaction() {
    m_TransactionCommands.clear();
    m_TransactionName.clear();
    m_InTransaction = false;
}
const char* EditorCommandStack::GetUndoName() const {
    return CanUndo() ? m_Undo.back()->GetName() : "";
}
const char* EditorCommandStack::GetRedoName() const {
    return CanRedo() ? m_Redo.back()->GetName() : "";
}

// ==========================================================================
// LambdaEditorCommand
// ==========================================================================

LambdaEditorCommand::LambdaEditorCommand(std::string name, Function execute, Function undo)
    : m_Name(std::move(name)), m_Execute(std::move(execute)), m_Undo(std::move(undo)) {
}
bool LambdaEditorCommand::Execute(EditorContext& context) {
    return m_Execute && m_Execute(context);
}
bool LambdaEditorCommand::Undo(EditorContext& context) {
    return m_Undo && m_Undo(context);
}

// ==========================================================================
// SceneSnapshotCommand
// ==========================================================================

SceneSnapshotCommand::SceneSnapshotCommand(std::string name, std::string beforeJson, std::string afterJson,
                                           uint64_t beforeSelection, uint64_t afterSelection)
    : m_Name(std::move(name)), m_BeforeJson(std::move(beforeJson)), m_AfterJson(std::move(afterJson)),
      m_BeforeSelection(beforeSelection), m_AfterSelection(afterSelection) {
}
bool SceneSnapshotCommand::Execute(EditorContext& context) {
    return Apply(context, m_AfterJson, m_AfterSelection);
}
bool SceneSnapshotCommand::Undo(EditorContext& context) {
    return Apply(context, m_BeforeJson, m_BeforeSelection);
}
bool SceneSnapshotCommand::Apply(EditorContext& context, const std::string& json, uint64_t selection) {
    Scene* scene = context.GetScene();
    if (!scene || !SceneSerializer::LoadFromString(*scene, json))
        return false;
    if (selection && scene->FindByID(selection))
        context.GetSelection().SelectActorID(selection);
    else
        context.GetSelection().Clear();
    return true;
}

// ==========================================================================
// SetActorTransformCommand
// ==========================================================================

SetActorTransformCommand::SetActorTransformCommand(uint64_t actorID, Transform before, Transform after)
    : m_ActorID(actorID), m_Before(before), m_After(after) {
}
bool SetActorTransformCommand::Execute(EditorContext& context) {
    return Apply(context, m_After);
}
bool SetActorTransformCommand::Undo(EditorContext& context) {
    return Apply(context, m_Before);
}
bool SetActorTransformCommand::Apply(EditorContext& context, const Transform& transform) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->GetTransform() = transform;
    return true;
}

// ==========================================================================
// CreateActorCommand
// ==========================================================================

CreateActorCommand::CreateActorCommand(const ActorCreateDesc& desc, uint64_t newID) : m_Desc(desc), m_ActorID(newID) {
}
bool CreateActorCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    if (!scene)
        return false;
    auto desc = m_Desc;
    desc.persistentID = m_ActorID;
    ActorHandle handle = scene->QueueCreateActor(desc);
    if (!handle || !scene->FlushCommands())
        return false;
    m_WasCreated = true;
    context.GetSelection().SelectActorID(m_ActorID);
    return true;
}
bool CreateActorCommand::Undo(EditorContext& context) {
    if (!m_WasCreated)
        return false;
    Scene* scene = context.GetScene();
    if (!scene)
        return false;
    Actor* actor = scene->FindByID(m_ActorID);
    if (!actor)
        return false;
    scene->QueueDestroyActor(actor->GetHandle());
    scene->FlushCommands();
    m_WasCreated = false;
    return true;
}

// ==========================================================================
// DestroyActorCommand
// ==========================================================================

DestroyActorCommand::DestroyActorCommand(uint64_t actorID, const std::string& subtreeJson, uint64_t parentID)
    : m_ActorID(actorID), m_SubtreeJson(subtreeJson), m_ParentID(parentID) {
}
bool DestroyActorCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    if (!scene)
        return false;
    Actor* actor = scene->FindByID(m_ActorID);
    if (!actor)
        return false;
    scene->QueueDestroyActor(actor->GetHandle());
    if (!scene->FlushCommands())
        return false;
    m_WasDestroyed = true;
    context.GetSelection().Clear();
    return true;
}
bool DestroyActorCommand::Undo(EditorContext& context) {
    if (!m_WasDestroyed)
        return false;
    Scene* scene = context.GetScene();
    if (!scene)
        return false;
    if (!ReconstructSubtree(*scene))
        return false;
    m_WasDestroyed = false;
    context.GetSelection().SelectActorID(m_ActorID);
    return true;
}
bool DestroyActorCommand::ReconstructSubtree(Scene& scene) {
    try {
        nlohmann::json arr = nlohmann::json::parse(m_SubtreeJson);
        if (!arr.is_array() || arr.empty())
            return false;
        // Map original ID -> reserved Handle
        std::unordered_map<uint64_t, ActorHandle> handles;
        // Pass 1: reserve handles for every actor
        for (const auto& a : arr) {
            uint64_t id = a.value("id", uint64_t(0));
            std::string name = a.value("name", std::string("Actor"));
            bool active = a.value("active", true);
            ActorCreateDesc desc;
            desc.name = name;
            desc.persistentID = id;
            desc.activeSelf = active;
            desc.editorFlags = a.value("editorFlags", uint32_t{0});
            if (a.contains("transform")) {
                const auto& t = a["transform"];
                desc.transform.position = Vec3FromJsonArray(t["position"]);
                desc.transform.rotation = Vec3FromJsonArray(t["rotation"]);
                desc.transform.scale = Vec3FromJsonArray(t["scale"]);
            }
            if (a.contains("components") && a["components"].is_array()) {
                for (const auto& c : a["components"]) {
                    ComponentCreateDesc cd;
                    cd.type = c.value("type", std::string{});
                    cd.enabled = c.value("enabled", true);
                    cd.data = c.contains("data") ? c["data"] : nlohmann::json::object();
                    if (!cd.type.empty())
                        desc.components.push_back(cd);
                }
            }
            handles[id] = scene.QueueCreateActor(desc);
        }
        // Pass 2: restore parenting
        for (const auto& a : arr) {
            uint64_t id = a.value("id", uint64_t(0));
            uint64_t pid = a.value("parentID", uint64_t(0));
            if (pid != 0) {
                auto childIt = handles.find(id);
                auto parentIt = handles.find(pid);
                if (childIt != handles.end() && parentIt != handles.end())
                    scene.QueueSetParent(childIt->second, parentIt->second);
            }
        }
        // Reparent root to its original parent
        if (m_ParentID != 0) {
            Actor* parent = scene.FindByID(m_ParentID);
            auto rootIt = handles.find(m_ActorID);
            if (parent && rootIt != handles.end())
                scene.QueueSetParent(rootIt->second, parent->GetHandle());
        }
        return scene.FlushCommands();
    } catch (const std::exception& e) {
        Logger::Error("[DestroyActorCommand] Reconstruction failed: ", e.what());
        return false;
    }
}

// ==========================================================================
// SetParentCommand
// ==========================================================================

SetParentCommand::SetParentCommand(uint64_t childID, uint64_t beforeParentID, uint64_t afterParentID)
    : m_ChildID(childID), m_BeforeParentID(beforeParentID), m_AfterParentID(afterParentID) {
}
bool SetParentCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* child = scene ? scene->FindByID(m_ChildID) : nullptr;
    if (!child)
        return false;
    Actor* parent = m_AfterParentID ? scene->FindByID(m_AfterParentID) : nullptr;
    scene->QueueSetParent(child->GetHandle(), parent ? parent->GetHandle() : ActorHandle{});
    scene->FlushCommands();
    context.GetSelection().SelectActorID(m_ChildID);
    return true;
}
bool SetParentCommand::Undo(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* child = scene ? scene->FindByID(m_ChildID) : nullptr;
    if (!child)
        return false;
    Actor* parent = m_BeforeParentID ? scene->FindByID(m_BeforeParentID) : nullptr;
    scene->QueueSetParent(child->GetHandle(), parent ? parent->GetHandle() : ActorHandle{});
    scene->FlushCommands();
    context.GetSelection().SelectActorID(m_ChildID);
    return true;
}

// ==========================================================================
// MoveActorCommand
// ==========================================================================

MoveActorCommand::MoveActorCommand(uint64_t childID, uint64_t beforeParentID, uint64_t beforeNextSiblingID,
                                   uint64_t afterParentID, uint64_t afterNextSiblingID)
    : m_ChildID(childID), m_BeforeParentID(beforeParentID), m_BeforeNextSiblingID(beforeNextSiblingID),
      m_AfterParentID(afterParentID), m_AfterNextSiblingID(afterNextSiblingID) {
}

bool MoveActorCommand::Apply(EditorContext& context, uint64_t parentID, uint64_t nextSiblingID) {
    Scene* scene = context.GetScene();
    Actor* child = scene ? scene->FindByID(m_ChildID) : nullptr;
    if (!child)
        return false;
    Actor* parent = parentID ? scene->FindByID(parentID) : nullptr;
    Actor* nextSibling = nextSiblingID ? scene->FindByID(nextSiblingID) : nullptr;
    scene->QueueMoveActor(child->GetHandle(), parent ? parent->GetHandle() : ActorHandle{},
                          nextSibling ? nextSibling->GetHandle() : ActorHandle{});
    scene->FlushCommands();
    context.GetSelection().SelectActorID(m_ChildID);
    return true;
}

bool MoveActorCommand::Execute(EditorContext& context) {
    return Apply(context, m_AfterParentID, m_AfterNextSiblingID);
}

bool MoveActorCommand::Undo(EditorContext& context) {
    return Apply(context, m_BeforeParentID, m_BeforeNextSiblingID);
}

// ==========================================================================
// SetActorActiveCommand
// ==========================================================================

SetActorActiveCommand::SetActorActiveCommand(uint64_t actorID, bool beforeActive, bool afterActive)
    : m_ActorID(actorID), m_BeforeActive(beforeActive), m_AfterActive(afterActive) {
}
bool SetActorActiveCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->SetActive(m_AfterActive);
    return true;
}
bool SetActorActiveCommand::Undo(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->SetActive(m_BeforeActive);
    return true;
}

// ==========================================================================
// SetActorNameCommand
// ==========================================================================

SetActorNameCommand::SetActorNameCommand(uint64_t actorID, std::string beforeName, std::string afterName)
    : m_ActorID(actorID), m_BeforeName(std::move(beforeName)), m_AfterName(std::move(afterName)) {
}
bool SetActorNameCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->SetName(m_AfterName);
    return true;
}
bool SetActorNameCommand::Undo(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->SetName(m_BeforeName);
    return true;
}

// ==========================================================================
// AddComponentCommand
// ==========================================================================

AddComponentCommand::AddComponentCommand(uint64_t actorID, std::string typeName, const nlohmann::json& initialData)
    : m_ActorID(actorID), m_TypeName(std::move(typeName)), m_InitialData(initialData) {
}
bool AddComponentCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    Component* comp = ComponentRegistry::Get().Create(m_TypeName, *actor);
    if (!comp)
        return false;
    if (!m_InitialData.is_null() && !m_InitialData.empty())
        comp->Deserialize(m_InitialData);
    m_WasAdded = true;
    return true;
}
bool AddComponentCommand::Undo(EditorContext& context) {
    if (!m_WasAdded)
        return false;
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->RemoveComponentByTypeName(m_TypeName);
    m_WasAdded = false;
    return true;
}

// ==========================================================================
// RemoveComponentCommand
// ==========================================================================

RemoveComponentCommand::RemoveComponentCommand(uint64_t actorID, std::string typeName,
                                               const nlohmann::json& serializedData)
    : m_ActorID(actorID), m_TypeName(std::move(typeName)), m_SerializedData(serializedData) {
}
bool RemoveComponentCommand::Execute(EditorContext& context) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    actor->RemoveComponentByTypeName(m_TypeName);
    m_WasRemoved = true;
    return true;
}
bool RemoveComponentCommand::Undo(EditorContext& context) {
    if (!m_WasRemoved)
        return false;
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    if (!actor)
        return false;
    Component* comp = ComponentRegistry::Get().Create(m_TypeName, *actor);
    if (!comp)
        return false;
    if (!m_SerializedData.is_null() && !m_SerializedData.empty())
        comp->Deserialize(m_SerializedData);
    m_WasRemoved = false;
    return true;
}

// ==========================================================================
// SetComponentPropertyCommand
// ==========================================================================

SetComponentPropertyCommand::SetComponentPropertyCommand(uint64_t actorID, std::string componentType,
                                                         std::string propertyName, nlohmann::json beforeJson,
                                                         nlohmann::json afterJson)
    : m_ActorID(actorID), m_ComponentType(std::move(componentType)), m_PropertyName(std::move(propertyName)),
      m_BeforeJson(std::move(beforeJson)), m_AfterJson(std::move(afterJson)) {
}
bool SetComponentPropertyCommand::Execute(EditorContext& context) {
    return ApplyJson(context, m_AfterJson);
}
bool SetComponentPropertyCommand::Undo(EditorContext& context) {
    return ApplyJson(context, m_BeforeJson);
}
const char* SetComponentPropertyCommand::GetName() const {
    if (!m_PropertyName.empty()) {
        static thread_local std::string s_Name;
        s_Name = "Set " + m_PropertyName;
        return s_Name.c_str();
    }
    return "Set Property";
}
bool SetComponentPropertyCommand::ApplyJson(EditorContext& context, const nlohmann::json& json) {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(m_ActorID) : nullptr;
    Component* comp = actor ? actor->GetComponentByTypeName(m_ComponentType) : nullptr;
    if (!comp)
        return false;
    comp->Deserialize(json);
    return true;
}

ModifyAssetCommand::ModifyAssetCommand(std::string assetPath, std::string beforeContent, std::string afterContent)
    : m_AssetPath(std::move(assetPath)), m_BeforeContent(std::move(beforeContent)),
      m_AfterContent(std::move(afterContent)) {
}

bool ModifyAssetCommand::Execute(EditorContext& context) {
    std::ofstream output(m_AssetPath, std::ios::binary | std::ios::trunc);
    output.write(m_AfterContent.data(), static_cast<std::streamsize>(m_AfterContent.size()));
    output.close();
    if (!output.good())
        return false;
    RefreshModifiedAsset(context, m_AssetPath);
    return true;
}

bool ModifyAssetCommand::Undo(EditorContext& context) {
    std::ofstream output(m_AssetPath, std::ios::binary | std::ios::trunc);
    output.write(m_BeforeContent.data(), static_cast<std::streamsize>(m_BeforeContent.size()));
    output.close();
    if (!output.good())
        return false;
    RefreshModifiedAsset(context, m_AssetPath);
    return true;
}

ModifyAssetsCommand::ModifyAssetsCommand(std::vector<Entry> entries) : m_Entries(std::move(entries)) {
    if (!m_Entries.empty())
        m_ResourcePath = m_Entries.front().assetPath;
}

bool ModifyAssetsCommand::Execute(EditorContext& context) {
    return Apply(context, false);
}

bool ModifyAssetsCommand::Undo(EditorContext& context) {
    return Apply(context, true);
}

bool ModifyAssetsCommand::Apply(EditorContext& context, bool undo) {
    std::vector<Entry> applied;
    applied.reserve(m_Entries.size());
    for (const Entry& entry : m_Entries) {
        const std::string& content = undo ? entry.beforeContent : entry.afterContent;
        std::ofstream output(entry.assetPath, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.close();
        if (!output.good()) {
            for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
                const std::string& rollback = undo ? it->afterContent : it->beforeContent;
                std::ofstream restore(it->assetPath, std::ios::binary | std::ios::trunc);
                restore.write(rollback.data(), static_cast<std::streamsize>(rollback.size()));
            }
            return false;
        }
        applied.push_back(entry);
    }
    RefreshAssetRegistry(context);
    return true;
}

CreateAssetCommand::CreateAssetCommand(std::string assetPath, std::string content)
    : m_AssetPath(std::move(assetPath)), m_Content(std::move(content)) {
}

bool CreateAssetCommand::Execute(EditorContext& context) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_AssetPath).parent_path(), error);
    if (error)
        return false;
    std::ofstream output(m_AssetPath, std::ios::binary | std::ios::trunc);
    output.write(m_Content.data(), static_cast<std::streamsize>(m_Content.size()));
    m_Created = output.good();
    output.close();
    if (m_Created && !std::filesystem::exists(AssetMeta::MetaPathFor(m_AssetPath))) {
        AssetMeta meta = AssetMeta::Create(m_AssetPath);
        m_Created = AssetMeta::Save(meta);
        if (!m_Created)
            std::filesystem::remove(m_AssetPath, error);
    }
    if (m_Created)
        context.GetSelection().SelectAssetPath(m_AssetPath);
    return m_Created;
}

bool CreateAssetCommand::Undo(EditorContext& context) {
    if (!m_Created)
        return false;
    std::error_code error;
    const bool removed = std::filesystem::remove(m_AssetPath, error);
    if (error || !removed)
        return false;
    std::filesystem::remove(AssetMeta::MetaPathFor(m_AssetPath), error);
    RetargetAssetSelection(context, m_AssetPath, "");
    m_Created = false;
    return true;
}

DeleteAssetCommand::DeleteAssetCommand(std::string assetPath, std::string content)
    : m_AssetPath(std::move(assetPath)), m_Content(std::move(content)) {
}

bool DeleteAssetCommand::Execute(EditorContext& context) {
    std::error_code error;
    const std::string metaPath = AssetMeta::MetaPathFor(m_AssetPath);
    m_HadMeta = std::filesystem::is_regular_file(metaPath, error);
    if (m_HadMeta) {
        std::ifstream input(metaPath, std::ios::binary);
        m_MetaContent.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    m_RemovedDatabaseRecords = CaptureAssetDatabaseRecordsForPath(context, m_AssetPath);
    if (!RemoveAssetDatabaseRecords(context, m_RemovedDatabaseRecords))
        return false;
    const bool removed = std::filesystem::remove(m_AssetPath, error);
    m_Deleted = removed && !error;
    if (!m_Deleted) {
        RestoreAssetDatabaseRecords(context, m_RemovedDatabaseRecords);
        return false;
    }
    if (m_Deleted && m_HadMeta) {
        std::filesystem::remove(metaPath, error);
        if (error) {
            RestoreAssetDatabaseRecords(context, m_RemovedDatabaseRecords);
            return false;
        }
    }
    RetargetAssetSelection(context, m_AssetPath, "");
    RefreshAssetRegistry(context);
    return m_Deleted;
}

bool DeleteAssetCommand::Undo(EditorContext& context) {
    if (!m_Deleted)
        return false;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(m_AssetPath).parent_path(), error);
    if (error)
        return false;
    std::ofstream output(m_AssetPath, std::ios::binary | std::ios::trunc);
    output.write(m_Content.data(), static_cast<std::streamsize>(m_Content.size()));
    if (!output.good())
        return false;
    output.close();
    if (m_HadMeta) {
        std::ofstream metaOutput(AssetMeta::MetaPathFor(m_AssetPath), std::ios::binary | std::ios::trunc);
        metaOutput.write(m_MetaContent.data(), static_cast<std::streamsize>(m_MetaContent.size()));
        if (!metaOutput.good())
            return false;
    }
    if (!RestoreAssetDatabaseRecords(context, m_RemovedDatabaseRecords))
        return false;
    context.GetSelection().SelectAssetPath(m_AssetPath);
    RefreshAssetRegistry(context);
    m_Deleted = false;
    return true;
}

RenameAssetCommand::RenameAssetCommand(std::string oldPath, std::string newPath)
    : m_OldPath(std::move(oldPath)), m_NewPath(std::move(newPath)) {
}

bool RenameAssetCommand::Execute(EditorContext& context) {
    std::error_code error;
    std::filesystem::rename(m_OldPath, m_NewPath, error);
    if (!error) {
        const std::string oldMeta = AssetMeta::MetaPathFor(m_OldPath);
        if (std::filesystem::exists(oldMeta))
            std::filesystem::rename(oldMeta, AssetMeta::MetaPathFor(m_NewPath), error);
    }
    if (error)
        return false;
    if (!RetargetAssetDatabasePaths(context, m_OldPath, m_NewPath)) {
        std::filesystem::rename(m_NewPath, m_OldPath, error);
        if (!error) {
            const std::string newMeta = AssetMeta::MetaPathFor(m_NewPath);
            if (std::filesystem::exists(newMeta))
                std::filesystem::rename(newMeta, AssetMeta::MetaPathFor(m_OldPath), error);
        }
        return false;
    }
    m_Renamed = true;
    RetargetAssetSelection(context, m_OldPath, m_NewPath);
    RefreshAssetRegistry(context);
    return true;
}

bool RenameAssetCommand::Undo(EditorContext& context) {
    if (!m_Renamed)
        return false;
    std::error_code error;
    std::filesystem::rename(m_NewPath, m_OldPath, error);
    if (!error) {
        const std::string newMeta = AssetMeta::MetaPathFor(m_NewPath);
        if (std::filesystem::exists(newMeta))
            std::filesystem::rename(newMeta, AssetMeta::MetaPathFor(m_OldPath), error);
    }
    if (error)
        return false;
    if (!RetargetAssetDatabasePaths(context, m_NewPath, m_OldPath)) {
        std::filesystem::rename(m_OldPath, m_NewPath, error);
        if (!error) {
            const std::string oldMeta = AssetMeta::MetaPathFor(m_OldPath);
            if (std::filesystem::exists(oldMeta))
                std::filesystem::rename(oldMeta, AssetMeta::MetaPathFor(m_NewPath), error);
        }
        return false;
    }
    m_Renamed = false;
    RetargetAssetSelection(context, m_NewPath, m_OldPath);
    RefreshAssetRegistry(context);
    return true;
}
