#include "Scene/PrefabSystem.h"
#include "Scene/TypeRegistry.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/ActorSubtreeSerializer.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {
void SetError(std::string* error, const std::string& value) {
    if (error)
        *error = value;
}
bool EqualFloat(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}
bool EqualVec3(const Vec3& a, const Vec3& b) {
    return EqualFloat(a.x, b.x) && EqualFloat(a.y, b.y) && EqualFloat(a.z, b.z);
}
bool EqualTransform(const Transform& a, const Transform& b) {
    return EqualVec3(a.position, b.position) && EqualVec3(a.rotation, b.rotation) && EqualVec3(a.scale, b.scale);
}
nlohmann::json Vec3Json(const Vec3& value) {
    return {value.x, value.y, value.z};
}
nlohmann::json TransformJson(const Transform& value) {
    return {{"position", Vec3Json(value.position)},
            {"rotation", Vec3Json(value.rotation)},
            {"scale", Vec3Json(value.scale)}};
}
Vec3 ReadVec3(const nlohmann::json& value, Vec3 fallback) {
    if (!value.is_array() || value.size() < 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number())
        return fallback;
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}
bool ReadTransform(const nlohmann::json& value, Transform& result) {
    if (!value.is_object())
        return false;
    result.position = ReadVec3(value.value("position", nlohmann::json::array()), {});
    result.rotation = ReadVec3(value.value("rotation", nlohmann::json::array()), {});
    result.scale = ReadVec3(value.value("scale", nlohmann::json::array()), {1, 1, 1});
    return true;
}
std::string EscapeToken(const std::string& token) {
    std::string out;
    for (char value : token) {
        if (value == '~')
            out += "~0";
        else if (value == '/')
            out += "~1";
        else
            out += value;
    }
    return out;
}
void DiffJson(const nlohmann::json& source, const nlohmann::json& current, const std::string& path,
              const std::string& localId, const std::string& componentType, nlohmann::json& overrides) {
    if (source.type() != current.type()) {
        overrides.push_back({{"kind", "SetProperty"},
                             {"localId", localId},
                             {"componentType", componentType},
                             {"path", path},
                             {"value", current}});
        return;
    }
    if (source.is_object()) {
        std::unordered_set<std::string> keys;
        for (auto it = source.begin(); it != source.end(); ++it)
            keys.insert(it.key());
        for (auto it = current.begin(); it != current.end(); ++it)
            keys.insert(it.key());
        for (const auto& key : keys) {
            const std::string childPath = path + "/" + EscapeToken(key);
            if (source.contains(key) && !current.contains(key)) {
                overrides.push_back({{"kind", "RemoveProperty"},
                                     {"localId", localId},
                                     {"componentType", componentType},
                                     {"path", childPath}});
            } else if (!source.contains(key)) {
                overrides.push_back({{"kind", "SetProperty"},
                                     {"localId", localId},
                                     {"componentType", componentType},
                                     {"path", childPath},
                                     {"value", current.contains(key) ? current[key] : nlohmann::json()}});
            } else
                DiffJson(source[key], current[key], childPath, localId, componentType, overrides);
        }
    } else if (source.is_array()) {
        if (source.size() != current.size()) {
            overrides.push_back({{"kind", "SetProperty"},
                                 {"localId", localId},
                                 {"componentType", componentType},
                                 {"path", path},
                                 {"value", current}});
        } else
            for (size_t i = 0; i < source.size(); ++i)
                DiffJson(source[i], current[i], path + "/" + std::to_string(i), localId, componentType, overrides);
    } else if (source != current) {
        overrides.push_back({{"kind", "SetProperty"},
                             {"localId", localId},
                             {"componentType", componentType},
                             {"path", path},
                             {"value", current}});
    }
}
PrefabNode* FindNode(PrefabAsset& asset, const std::string& id) {
    for (auto& node : asset.nodes)
        if (node.localId == id)
            return &node;
    return nullptr;
}
const PrefabNode* FindNode(const PrefabAsset& asset, const std::string& id) {
    for (const auto& node : asset.nodes)
        if (node.localId == id)
            return &node;
    return nullptr;
}
ComponentCreateDesc* FindComponent(PrefabNode& node, const std::string& type) {
    for (auto& component : node.components)
        if (component.type == type)
            return &component;
    return nullptr;
}
bool RemoveNodeSubtree(PrefabAsset& asset, const std::string& id) {
    if (id.empty() || id == asset.rootLocalId || !FindNode(asset, id))
        return false;
    std::unordered_set<std::string> removeIds{id};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const PrefabNode& node : asset.nodes) {
            if (!removeIds.count(node.localId) && removeIds.count(node.parentLocalId)) {
                removeIds.insert(node.localId);
                changed = true;
            }
        }
    }
    const size_t before = asset.nodes.size();
    asset.nodes.erase(std::remove_if(asset.nodes.begin(), asset.nodes.end(),
                                     [&](const PrefabNode& node) { return removeIds.count(node.localId) != 0; }),
                      asset.nodes.end());
    return asset.nodes.size() != before;
}
bool ApplySet(nlohmann::json& target, const std::string& path, const nlohmann::json& value) {
    try {
        if (path.empty()) {
            target = value;
            return true;
        }
        target[nlohmann::json::json_pointer(path)] = value;
        return true;
    } catch (...) {
        return false;
    }
}
std::string UnescapeToken(std::string token) {
    size_t position = 0;
    while ((position = token.find('~', position)) != std::string::npos && position + 1 < token.size()) {
        if (token[position + 1] == '0')
            token.replace(position, 2, "~");
        else if (token[position + 1] == '1')
            token.replace(position, 2, "/");
        ++position;
    }
    return token;
}
bool ApplyRemove(nlohmann::json& target, const std::string& path) {
    try {
        const size_t slash = path.rfind('/');
        if (slash == std::string::npos)
            return false;
        const std::string parentPath = path.substr(0, slash), token = UnescapeToken(path.substr(slash + 1));
        nlohmann::json& parent = parentPath.empty() ? target : target[nlohmann::json::json_pointer(parentPath)];
        if (parent.is_object()) {
            parent.erase(token);
            return true;
        }
        if (parent.is_array()) {
            const size_t index = std::stoull(token);
            if (index >= parent.size())
                return false;
            parent.erase(parent.begin() + static_cast<nlohmann::json::difference_type>(index));
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
bool ApplyOverrides(PrefabAsset& asset, const nlohmann::json& overrides, std::string* error) {
    if (!overrides.is_array()) {
        SetError(error, "prefab overrides must be an array");
        return false;
    }
    for (const auto& item : overrides) {
        const std::string kind = item.value("kind", std::string{}), id = item.value("localId", std::string{}),
                          type = item.value("componentType", std::string{});
        if (kind == "SetNestedInstance") {
            const std::string nestedId = item.value("nestedInstanceLocalId", std::string{});
            auto nested =
                std::find_if(asset.nestedInstances.begin(), asset.nestedInstances.end(),
                             [&](const PrefabNestedInstance& value) { return value.instanceLocalId == nestedId; });
            if (nested == asset.nestedInstances.end()) {
                SetError(error, "nested prefab override target is missing: " + nestedId);
                return false;
            }
            if (!item.contains("overrides") || !item["overrides"].is_array()) {
                SetError(error, "nested prefab override payload must be an array");
                return false;
            }
            nested->overrides = item["overrides"];
            if (item.contains("rootTransform") && !ReadTransform(item["rootTransform"], nested->rootTransform)) {
                SetError(error, "nested prefab root transform is invalid");
                return false;
            }
            continue;
        }
        PrefabNode* node = FindNode(asset, id);
        if (kind == "AddActorSubtree") {
            const std::string parentId = item.value("parentLocalId", id);
            if (!item.contains("nodes") || !item["nodes"].is_array())
                continue;
            for (const auto& value : item["nodes"]) {
                PrefabNode added;
                if (!PrefabNodeFromJson(value, added, error))
                    return false;
                if (added.parentLocalId.empty())
                    added.parentLocalId = parentId;
                asset.nodes.push_back(std::move(added));
            }
            continue;
        }
        if (kind == "RemoveActorSubtree") {
            RemoveNodeSubtree(asset, id);
            continue;
        }
        if (!node) {
            Logger::Warn("[Prefab] unresolved override node: ", id);
            continue;
        }
        if (kind == "RemoveComponent") {
            node->components.erase(std::remove_if(node->components.begin(), node->components.end(),
                                                  [&](const auto& c) { return c.type == type; }),
                                   node->components.end());
        } else if (kind == "AddComponent") {
            if (!FindComponent(*node, type))
                node->components.push_back(
                    {type, item.value("enabled", true), item.value("data", nlohmann::json::object())});
        } else if (kind == "SetProperty" || kind == "RemoveProperty") {
            const std::string path = item.value("path", std::string{});
            const nlohmann::json value = item.value("value", nlohmann::json());
            if (type.empty()) {
                auto json = PrefabNodeToJson(*node);
                if ((kind == "RemoveProperty" ? ApplyRemove(json, path) : ApplySet(json, path, value)))
                    PrefabNodeFromJson(json, *node, error);
            } else if (auto* component = FindComponent(*node, type)) {
                if (path == "/enabled")
                    component->enabled = value.get<bool>();
                else if (!(kind == "RemoveProperty" ? ApplyRemove(component->data, path)
                                                    : ApplySet(component->data, path, value)))
                    Logger::Warn("[Prefab] invalid override path: ", path);
            }
        }
    }
    return true;
}
std::unordered_map<std::string, Actor*> InstanceActors(Actor& root) {
    std::unordered_map<std::string, Actor*> result;
    Scene* scene = root.GetScene();
    if (!scene)
        return result;
    scene->ForEach([&](Actor& actor) {
        if (actor.GetPrefabInstanceRoot() == root.GetHandle())
            result[actor.GetPrefabLocalId()] = &actor;
    });
    return result;
}
std::unordered_map<std::string, const Actor*> InstanceActors(const Actor& root) {
    std::unordered_map<std::string, const Actor*> result;
    Scene* scene = root.GetScene();
    if (!scene)
        return result;
    scene->ForEach([&](Actor& actor) {
        if (actor.GetPrefabInstanceRoot() == root.GetHandle())
            result[actor.GetPrefabLocalId()] = &actor;
    });
    return result;
}

std::filesystem::path ResolveNestedPath(const std::filesystem::path& path) {
    if (path.is_absolute())
        return path.lexically_normal();
    return std::filesystem::path(AssetManager::Get().ResolvePath(path.generic_string())).lexically_normal();
}

bool ValidatePrefabDependencies(const std::filesystem::path& path, const PrefabAsset* candidate,
                                std::unordered_set<std::string>& active, std::unordered_set<std::string>& complete,
                                std::string* error) {
    PrefabAsset loaded;
    const PrefabAsset* asset = candidate;
    if (!asset && !PrefabAsset::Load(path, loaded, error))
        return false;
    if (!asset)
        asset = &loaded;
    if (complete.count(asset->uuid))
        return true;
    if (!active.insert(asset->uuid).second) {
        SetError(error, "nested prefab dependency cycle at " + path.generic_string());
        return false;
    }
    for (const auto& nested : asset->nestedInstances) {
        const auto nestedPath = ResolveNestedPath(nested.assetPath);
        PrefabAsset child;
        if (!PrefabAsset::Load(nestedPath, child, error) || child.uuid != nested.assetUuid) {
            if (error && error->empty())
                SetError(error, "nested prefab UUID mismatch");
            active.erase(asset->uuid);
            return false;
        }
        if (!ValidatePrefabDependencies(nestedPath, &child, active, complete, error)) {
            active.erase(asset->uuid);
            return false;
        }
    }
    active.erase(asset->uuid);
    complete.insert(asset->uuid);
    return true;
}
} // namespace

std::filesystem::path PrefabSystem::ResolvePrefabPath(const std::filesystem::path& path) {
    if (path.is_absolute())
        return path.lexically_normal();
    return std::filesystem::path(AssetManager::Get().ResolvePath(path.generic_string())).lexically_normal();
}

bool PrefabSystem::ApplyOverridesToAsset(PrefabAsset& asset, const nlohmann::json& overrides, std::string* error) {
    return ApplyOverrides(asset, overrides, error);
}

bool PrefabSystem::SetInstanceOverrides(Actor& instanceRoot, nlohmann::json overrides, std::string* error) {
    if (!instanceRoot.IsPrefabRoot()) {
        SetError(error, "actor is not a prefab instance root");
        return false;
    }
    if (!overrides.is_array()) {
        SetError(error, "prefab overrides must be an array");
        return false;
    }
    instanceRoot.m_PrefabOverrides = std::move(overrides);
    return true;
}

bool PrefabSystem::SaveSubtree(const Actor& root, const std::filesystem::path& path, std::string* error) {
    PrefabAsset asset;
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const AssetMeta meta = AssetMeta::LoadOrCreate(absolute.string());
    asset.uuid = meta.uuid;
    PrefabAsset previous;
    if (std::filesystem::is_regular_file(absolute) && PrefabAsset::Load(absolute, previous, nullptr))
        asset.revision = previous.revision + 1;
    std::vector<const Actor*> nestedRoots;
    std::function<void(const Actor&)> inspect = [&](const Actor& actor) {
        if (&actor != &root && actor.IsPrefabRoot()) {
            nestedRoots.push_back(&actor);
            return;
        }
        for (Actor* child : actor.GetChildren())
            inspect(*child);
    };
    inspect(root);
    std::unordered_set<const Actor*> excluded(nestedRoots.begin(), nestedRoots.end());
    std::unordered_map<const Actor*, std::string> localIds;
    if (!ActorSubtreeSerializer::Serialize(root, asset.nodes, error, &excluded, &localIds))
        return false;
    asset.rootLocalId = asset.nodes.front().localId;
    for (const Actor* nestedRoot : nestedRoots) {
        const Actor* parent = nestedRoot->GetParent();
        const auto parentId = localIds.find(parent);
        if (parentId == localIds.end()) {
            SetError(error, "nested prefab parent is outside saved subtree");
            return false;
        }
        PrefabAsset source;
        const auto sourcePath = ResolvePrefabPath(nestedRoot->GetPrefabAssetPath());
        if (!PrefabAsset::Load(sourcePath, source, error) || source.uuid != nestedRoot->GetPrefabAssetUuid()) {
            if (error && error->empty())
                SetError(error, "nested prefab source UUID mismatch");
            return false;
        }
        PrefabNestedInstance nested;
        nested.instanceLocalId = nestedRoot->GetNestedPrefabInstanceLocalId().empty()
                                     ? "nested-" + std::to_string(nestedRoot->GetID())
                                     : nestedRoot->GetNestedPrefabInstanceLocalId();
        nested.parentLocalId = parentId->second;
        nested.assetPath = AssetManager::Get().MakeProjectRelativePath(sourcePath.string());
        nested.assetUuid = source.uuid;
        nested.sourceRevision = source.revision;
        nested.rootTransform = nestedRoot->GetTransform();
        nested.overrides = nestedRoot->GetPrefabOverrides();
        asset.nestedInstances.push_back(std::move(nested));
    }
    std::unordered_set<std::string> active, complete;
    if (!asset.Validate(error) || !ValidatePrefabDependencies(absolute, &asset, active, complete, error))
        return false;
    return asset.Save(absolute, error);
}

ActorHandle PrefabSystem::QueueInstantiate(Scene& scene, const std::filesystem::path& path,
                                           const PrefabInstantiateOptions& options, std::string* error) {
    const auto absolute = ResolvePrefabPath(path);
    PrefabAsset asset;
    if (!PrefabAsset::Load(absolute, asset, error))
        return {};
    static thread_local std::vector<std::string> dependencyStack;
    if (dependencyStack.empty()) {
        std::unordered_set<std::string> active, complete;
        if (!ValidatePrefabDependencies(absolute, &asset, active, complete, error))
            return {};
    }
    const std::string dependencyKey = absolute.generic_string();
    if (std::find(dependencyStack.begin(), dependencyStack.end(), dependencyKey) != dependencyStack.end()) {
        SetError(error, "nested prefab dependency cycle at " + dependencyKey);
        return {};
    }
    struct StackScope {
        std::vector<std::string>& stack;
        ~StackScope() { stack.pop_back(); }
    };
    dependencyStack.push_back(dependencyKey);
    StackScope stackScope{dependencyStack};
    const AssetMeta meta = AssetMeta::LoadOrCreate(absolute.string());
    if (meta.uuid != asset.uuid) {
        SetError(error, "prefab UUID does not match its .meta file");
        return {};
    }
    if (!options.expectedUuid.empty() && options.expectedUuid != asset.uuid) {
        SetError(error, "prefab UUID does not match the scene reference");
        return {};
    }
    if (!ApplyOverrides(asset, options.overrides, error))
        return {};
    const std::string storedPath = AssetManager::Get().MakeProjectRelativePath(absolute.string());
    std::unordered_map<std::string, ActorHandle> handles;
    const PrefabNode* rootNode = nullptr;
    for (const auto& node : asset.nodes)
        if (node.localId == asset.rootLocalId) {
            rootNode = &node;
            break;
        }
    if (!rootNode) {
        SetError(error, "prefab root node is missing");
        return {};
    }
    ActorCreateDesc rootDesc;
    rootDesc.name = rootNode->name;
    rootDesc.transform = rootNode->transform;
    rootDesc.activeSelf = rootNode->activeSelf;
    rootDesc.editorFlags = rootNode->editorFlags;
    rootDesc.components = rootNode->components;
    rootDesc.prefabAssetPath = storedPath;
    rootDesc.prefabAssetUuid = asset.uuid;
    rootDesc.prefabLocalId = rootNode->localId;
    rootDesc.prefabOverrides = options.overrides;
    rootDesc.nestedPrefabInstanceLocalId = options.nestedInstanceLocalId;
    rootDesc.prefabRoot = true;
    rootDesc.persistentID = options.persistentRootID;
    if (options.rootTransform)
        rootDesc.transform = *options.rootTransform;
    const ActorHandle root = scene.QueueCreateActor(rootDesc);
    handles[rootNode->localId] = root;
    for (const auto& node : asset.nodes) {
        if (node.localId == asset.rootLocalId)
            continue;
        ActorCreateDesc desc;
        desc.name = node.name;
        desc.transform = node.transform;
        desc.activeSelf = node.activeSelf;
        desc.editorFlags = node.editorFlags;
        desc.components = node.components;
        desc.prefabAssetPath = storedPath;
        desc.prefabAssetUuid = asset.uuid;
        desc.prefabLocalId = node.localId;
        desc.prefabOverrides = nlohmann::json::array();
        desc.prefabInstanceRoot = root;
        handles[node.localId] = scene.QueueCreateActor(desc);
    }
    for (const auto& node : asset.nodes) {
        const ActorHandle child = handles[node.localId];
        if (node.localId == asset.rootLocalId) {
            if (options.parent)
                scene.QueueSetParent(child, options.parent);
        } else if (handles.count(node.parentLocalId))
            scene.QueueSetParent(child, handles[node.parentLocalId]);
    }
    for (const auto& nested : asset.nestedInstances) {
        const auto parent = handles.find(nested.parentLocalId);
        if (parent == handles.end()) {
            SetError(error, "nested prefab parent localId is missing");
            return {};
        }
        PrefabInstantiateOptions nestedOptions;
        nestedOptions.parent = parent->second;
        nestedOptions.rootTransform = nested.rootTransform;
        nestedOptions.expectedUuid = nested.assetUuid;
        nestedOptions.overrides = nested.overrides;
        nestedOptions.nestedInstanceLocalId = nested.instanceLocalId;
        if (!QueueInstantiate(scene, nested.assetPath, nestedOptions, error))
            return {};
    }
    return root;
}

Actor* PrefabSystem::Instantiate(Scene& scene, const std::filesystem::path& path,
                                 const PrefabInstantiateOptions& options, std::string* error) {
    const ActorHandle root = QueueInstantiate(scene, path, options, error);
    if (!root)
        return nullptr;
    if (!scene.FlushCommands()) {
        SetError(error, "cannot flush prefab creation during scene traversal");
        return nullptr;
    }
    return scene.TryGetActor(root);
}

bool PrefabSystem::CaptureOverrides(Actor& root, std::string* error) {
    nlohmann::json overrides = nlohmann::json::array();
    if (!BuildOverrides(root, overrides, error))
        return false;
    root.m_PrefabOverrides = std::move(overrides);
    return true;
}

bool PrefabSystem::BuildOverrides(const Actor& root, nlohmann::json& overrides, std::string* error) {
    if (!root.IsPrefabRoot()) {
        SetError(error, "actor is not a prefab instance root");
        return false;
    }
    PrefabAsset source;
    if (!PrefabAsset::Load(ResolvePrefabPath(root.m_PrefabAssetPath), source, error))
        return false;
    auto actors = InstanceActors(root);
    overrides = nlohmann::json::array();
    std::unordered_set<std::string> sourceIds;
    for (const auto& node : source.nodes) {
        sourceIds.insert(node.localId);
        auto found = actors.find(node.localId);
        if (found == actors.end())
            continue;
        const Actor& actor = *found->second;
        if (actor.GetName() != node.name)
            overrides.push_back({{"kind", "SetProperty"},
                                 {"localId", node.localId},
                                 {"componentType", ""},
                                 {"path", "/name"},
                                 {"value", actor.GetName()}});
        if (actor.IsActiveSelf() != node.activeSelf)
            overrides.push_back({{"kind", "SetProperty"},
                                 {"localId", node.localId},
                                 {"componentType", ""},
                                 {"path", "/active"},
                                 {"value", actor.IsActiveSelf()}});
        if (actor.GetEditorFlags() != node.editorFlags)
            overrides.push_back({{"kind", "SetProperty"},
                                 {"localId", node.localId},
                                 {"componentType", ""},
                                 {"path", "/editorFlags"},
                                 {"value", actor.GetEditorFlags()}});
        if (node.localId != source.rootLocalId) {
            if (!EqualVec3(actor.GetTransform().position, node.transform.position))
                overrides.push_back({{"kind", "SetProperty"},
                                     {"localId", node.localId},
                                     {"componentType", ""},
                                     {"path", "/transform/position"},
                                     {"value",
                                      {actor.GetTransform().position.x, actor.GetTransform().position.y,
                                       actor.GetTransform().position.z}}});
            if (!EqualVec3(actor.GetTransform().rotation, node.transform.rotation))
                overrides.push_back({{"kind", "SetProperty"},
                                     {"localId", node.localId},
                                     {"componentType", ""},
                                     {"path", "/transform/rotation"},
                                     {"value",
                                      {actor.GetTransform().rotation.x, actor.GetTransform().rotation.y,
                                       actor.GetTransform().rotation.z}}});
            if (!EqualVec3(actor.GetTransform().scale, node.transform.scale))
                overrides.push_back(
                    {{"kind", "SetProperty"},
                     {"localId", node.localId},
                     {"componentType", ""},
                     {"path", "/transform/scale"},
                     {"value",
                      {actor.GetTransform().scale.x, actor.GetTransform().scale.y, actor.GetTransform().scale.z}}});
        }
        std::unordered_map<std::string, const ComponentCreateDesc*> sourceComponents;
        for (const auto& c : node.components)
            sourceComponents[c.type] = &c;
        std::unordered_set<std::string> currentTypes;
        actor.ForEachComponent([&](Component& component) {
            const std::string type = component.GetTypeName();
            currentTypes.insert(type);
            nlohmann::json data = nlohmann::json::object();
            uint32_t version = 0;
            if (!TypeRegistry::Get().Serialize(component, data, version, nullptr))
                component.Serialize(data);
            auto it = sourceComponents.find(type);
            if (it == sourceComponents.end())
                overrides.push_back({{"kind", "AddComponent"},
                                     {"localId", node.localId},
                                     {"componentType", type},
                                     {"enabled", component.IsEnabled()},
                                     {"version", version},
                                     {"data", data}});
            else {
                if (component.IsEnabled() != it->second->enabled)
                    overrides.push_back({{"kind", "SetProperty"},
                                         {"localId", node.localId},
                                         {"componentType", type},
                                         {"path", "/enabled"},
                                         {"value", component.IsEnabled()}});
                DiffJson(it->second->data, data, "", node.localId, type, overrides);
            }
        });
        for (const auto& [type, value] : sourceComponents)
            if (!currentTypes.count(type))
                overrides.push_back({{"kind", "RemoveComponent"}, {"localId", node.localId}, {"componentType", type}});
    }
    for (const auto& node : source.nodes) {
        if (node.localId == source.rootLocalId || actors.count(node.localId))
            continue;
        const PrefabNode* parent = FindNode(source, node.parentLocalId);
        if (!parent)
            continue;
        if (parent->localId != source.rootLocalId && !actors.count(parent->localId))
            continue;
        overrides.push_back(
            {{"kind", "RemoveActorSubtree"}, {"localId", node.localId}, {"parentLocalId", node.parentLocalId}});
    }
    // Any source-missing descendant is a structural addition. Emit each added root once.
    std::vector<const Actor*> descendants;
    std::function<void(const Actor&)> collect = [&](const Actor& actor) {
        if (&actor != &root && actor.IsPrefabRoot())
            return;
        descendants.push_back(&actor);
        for (Actor* child : actor.GetChildren())
            collect(*child);
    };
    collect(root);
    for (const Actor* actor : descendants) {
        const std::string id = actor->GetPrefabLocalId();
        if (!id.empty() && sourceIds.count(id))
            continue;
        Actor* parent = actor->GetParent();
        if (!parent)
            continue;
        const std::string parentId = parent->GetPrefabLocalId();
        if (parent != &root && (parentId.empty() || !sourceIds.count(parentId)))
            continue;
        std::vector<PrefabNode> nodes;
        if (!ActorSubtreeSerializer::Serialize(*actor, nodes, error))
            return false;
        nlohmann::json values = nlohmann::json::array();
        for (const auto& node : nodes)
            values.push_back(PrefabNodeToJson(node));
        const std::string attachId = parent == &root ? source.rootLocalId : parentId;
        overrides.push_back({{"kind", "AddActorSubtree"},
                             {"localId", attachId},
                             {"parentLocalId", attachId},
                             {"nodes", std::move(values)}});
    }
    // Nested prefab roots are authoritative references in the outer prefab, not
    // flattened nodes. Capture their current local overrides and placement as a
    // typed outer-instance override so Scene snapshots and Undo/Redo retain them.
    for (const PrefabNestedInstance& sourceNested : source.nestedInstances) {
        const Actor* currentNested = nullptr;
        root.GetScene()->ForEach([&](Actor& actor) {
            if (currentNested || !actor.IsPrefabRoot() ||
                actor.GetNestedPrefabInstanceLocalId() != sourceNested.instanceLocalId)
                return;
            for (Actor* parent = actor.GetParent(); parent; parent = parent->GetParent()) {
                if (parent == &root) {
                    currentNested = &actor;
                    break;
                }
            }
        });
        if (!currentNested)
            continue;
        nlohmann::json nestedOverrides;
        if (!BuildOverrides(*currentNested, nestedOverrides, error))
            return false;
        if (nestedOverrides != sourceNested.overrides ||
            !EqualTransform(currentNested->GetTransform(), sourceNested.rootTransform)) {
            overrides.push_back({{"kind", "SetNestedInstance"},
                                 {"localId", sourceNested.parentLocalId},
                                 {"nestedInstanceLocalId", sourceNested.instanceLocalId},
                                 {"rootTransform", TransformJson(currentNested->GetTransform())},
                                 {"overrides", std::move(nestedOverrides)}});
        }
    }
    return true;
}

bool PrefabSystem::RefreshInstances(Scene& scene, const std::string& uuid, std::string* error) {
    struct Instance {
        std::string path;
        ActorHandle parent;
        Transform transform;
        uint64_t id;
        nlohmann::json overrides;
    };
    std::vector<Instance> instances;
    scene.ForEach([&](Actor& actor) {
        if (actor.IsPrefabRoot() && actor.GetPrefabAssetUuid() == uuid)
            instances.push_back({actor.GetPrefabAssetPath(),
                                 actor.GetParent() ? actor.GetParent()->GetHandle() : ActorHandle{},
                                 actor.GetTransform(), actor.GetID(), actor.GetPrefabOverrides()});
    });
    // Destroy and flush every old root before queueing replacements: persistent IDs, parent handles, and overrides are
    // reused by the new instances and must not resolve against the previous generation during construction.
    for (const auto& instance : instances)
        if (Actor* actor = scene.FindByID(instance.id))
            scene.QueueDestroyActor(actor->GetHandle());
    scene.FlushCommands();
    for (const auto& instance : instances) {
        PrefabInstantiateOptions options;
        options.parent = instance.parent;
        options.rootTransform = instance.transform;
        options.persistentRootID = instance.id;
        options.overrides = instance.overrides;
        if (!QueueInstantiate(scene, instance.path, options, error))
            return false;
    }
    return scene.FlushCommands();
}

bool PrefabSystem::ApplyAll(Actor& root, std::string* error) {
    if (!CaptureOverrides(root, error))
        return false;
    const auto path = ResolvePrefabPath(root.m_PrefabAssetPath);
    PrefabAsset source;
    if (!PrefabAsset::Load(path, source, error))
        return false;
    const PrefabNode* oldRoot = FindNode(source, source.rootLocalId);
    const Transform sourceRootTransform = oldRoot ? oldRoot->transform : Transform{};
    if (!SaveSubtree(root, path, error))
        return false;
    PrefabAsset updated;
    if (!PrefabAsset::Load(path, updated, error))
        return false;
    if (auto* updatedRoot = FindNode(updated, updated.rootLocalId))
        updatedRoot->transform = sourceRootTransform;
    if (!updated.Save(path, error))
        return false;
    root.m_PrefabOverrides = nlohmann::json::array();
    return RefreshInstances(*root.GetScene(), source.uuid, error);
}

bool PrefabSystem::RevertAll(Actor& root, std::string* error) {
    if (!root.IsPrefabRoot()) {
        SetError(error, "actor is not a prefab instance root");
        return false;
    }
    root.m_PrefabOverrides = nlohmann::json::array();
    return RefreshInstances(*root.GetScene(), root.GetPrefabAssetUuid(), error);
}

bool PrefabSystem::Unpack(Actor& root, std::string* error) {
    if (!root.IsPrefabRoot()) {
        SetError(error, "actor is not a prefab instance root");
        return false;
    }
    auto actors = InstanceActors(root);
    for (auto& [id, actor] : actors) {
        actor->m_PrefabAssetPath.clear();
        actor->m_PrefabAssetUuid.clear();
        actor->m_PrefabLocalId.clear();
        actor->m_PrefabInstanceRoot = {};
        actor->m_PrefabOverrides = nlohmann::json::array();
    }
    return true;
}
