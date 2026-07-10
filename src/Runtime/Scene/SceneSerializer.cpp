#include "Scene/SceneSerializer.h"
#include "Scene/Actor.h"
#include "Scene/Component.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/PrefabSystem.h"
#include "Assets/AssetManager.h"
#include "Assets/NavMeshAsset.h"
#include "Core/Logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using Json = nlohmann::json;

// --------------------------------------------------------------------------
// Helpers: Vec3 / Transform <-> JSON
// --------------------------------------------------------------------------

static Json Vec3ToJson(const Vec3& v) {
    return Json::array({ v.x, v.y, v.z });
}

static Vec3 Vec3FromJson(const Json& j) {
    Vec3 out;
    if (!j.is_array() || j.size() < 3 ||
        !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) {
        Logger::Warn("SceneSerializer: invalid Vec3 field, using default");
        return out;
    }
    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    out.z = j[2].get<float>();
    return out;
}

static Json TransformToJson(const Transform& t) {
    Json j;
    j["position"] = Vec3ToJson(t.position);
    j["rotation"] = Vec3ToJson(t.rotation);
    j["scale"]    = Vec3ToJson(t.scale);
    return j;
}

static Transform TransformFromJson(const Json& j) {
    Transform t;
    if (j.contains("position")) t.position = Vec3FromJson(j["position"]);
    if (j.contains("rotation")) t.rotation = Vec3FromJson(j["rotation"]);
    if (j.contains("scale"))    t.scale    = Vec3FromJson(j["scale"]);
    return t;
}

// --------------------------------------------------------------------------
// Serialize Scene → JSON object
// --------------------------------------------------------------------------

static Json SceneToJson(const Scene& scene)
{
    Json root;
    root["name"]   = scene.GetName();
    if (scene.GetMainCameraHintActorID() != 0) {
        root["mainCameraHintActorID"] = scene.GetMainCameraHintActorID();
    }
    if (scene.GetAmbientIntensity() != 1.0f) {
        root["ambientIntensity"] = scene.GetAmbientIntensity();
    }
    if(!scene.GetNavMeshAssetPath().empty())root["navMeshAsset"]=scene.GetNavMeshAssetPath();
    if(!scene.GetPreloadAssets().empty())root["preloadAssets"]=scene.GetPreloadAssets();

    Json actorsArr = Json::array();

    scene.ForEach([&](Actor& actor) {
        for (Actor* parent=actor.GetParent();parent;parent=parent->GetParent())
            if (parent->IsPrefabRoot()) return;
        if (actor.IsPrefabInstance() && !actor.IsPrefabRoot()) return;
        Json a;
        a["id"]       = actor.GetID();
        a["name"]     = actor.GetName();
        a["active"]   = actor.IsActiveSelf();
        if (!actor.GetTag().empty()) a["tag"] = actor.GetTag();
        if (actor.GetLayer() != 0) a["layer"] = actor.GetLayer();
        if (actor.GetEditorFlags() != 0) a["editorFlags"] = actor.GetEditorFlags();
        a["parentID"] = actor.GetParent() ? actor.GetParent()->GetID() : uint64_t(0);
        a["transform"] = TransformToJson(actor.GetTransform());

        if (actor.IsPrefabRoot()) {
            std::string error;
            if (!PrefabSystem::CaptureOverrides(actor, &error))
                throw std::runtime_error("failed to capture prefab overrides: " + error);
            a["prefabInstance"] = {
                {"asset", actor.GetPrefabAssetPath()},
                {"uuid", actor.GetPrefabAssetUuid()},
                {"overrides", actor.GetPrefabOverrides()}
            };
            actorsArr.push_back(std::move(a));
            return;
        }

        // Components
        Json comps = Json::array();
        actor.ForEachComponent([&](Component& comp) {
            Json c;
            c["type"]    = comp.GetTypeName();
            c["enabled"] = comp.IsEnabled();
            Json data    = Json::object();
            comp.Serialize(data);
            c["data"] = data;
            comps.push_back(c);
        });
        a["components"] = comps;

        actorsArr.push_back(a);
    });

    root["actors"] = actorsArr;

    // Record next ID so we can restore it exactly
    // Derive it from max existing ID + 1 (safe even without a getter)
    uint64_t maxID = 0;
    scene.ForEach([&](Actor& actor){
        if (actor.GetID() > maxID) maxID = actor.GetID();
    });
    root["nextID"] = maxID + 1;

    return root;
}

// --------------------------------------------------------------------------
// Deserialize JSON object → Scene
// --------------------------------------------------------------------------

static bool JsonToScene(const Json& root, Scene& scene)
{
    try {
        scene.Clear();
        scene.SetName(root.value("name", "Scene"));
        scene.SetMainCameraHintActorID(
            root.value("mainCameraHintActorID", uint64_t{0}));
        scene.SetAmbientIntensity(root.value("ambientIntensity", 1.0f));
        scene.SetNavMeshAssetPath(root.value("navMeshAsset",std::string{}));
        scene.SetPreloadAssets(root.value("preloadAssets",std::vector<std::string>{}));
        if(!scene.GetNavMeshAssetPath().empty()){auto nav=AssetManager::Get().Load<NavMeshAsset>(scene.GetNavMeshAssetPath());if(nav)nav->Apply(scene.GetNavigationWorld());}

        if (!root.contains("actors")) return true; // empty scene is valid

        const Json& actorsArr = root["actors"];
        if (!actorsArr.is_array()) {
            Logger::Warn("SceneSerializer: 'actors' is not an array");
            return true;
        }

        std::unordered_map<uint64_t, ActorHandle> handles;

        // Pass 1: reserve every handle and deserialize component payloads before lifecycle.
        for (const Json& a : actorsArr) {
            if (!a.is_object()) {
                Logger::Warn("SceneSerializer: skipping malformed actor entry");
                continue;
            }

            uint64_t    id     = a.value("id",     uint64_t(0));
            std::string name   = a.value("name",   std::string("Actor"));
            bool        active = a.value("active", true);

            if (a.contains("prefabInstance")) {
                const Json& prefab = a["prefabInstance"];
                if (!prefab.is_object()) throw std::runtime_error("prefabInstance must be an object");
                PrefabInstantiateOptions options;
                options.persistentRootID = id;
                options.rootTransform = a.contains("transform") ? TransformFromJson(a["transform"]) : Transform{};
                options.expectedUuid = prefab.value("uuid", std::string{});
                options.overrides = prefab.value("overrides", Json::array());
                std::string error;
                ActorHandle handle = PrefabSystem::QueueInstantiate(scene, prefab.value("asset", std::string{}), options, &error);
                if (!handle) throw std::runtime_error("failed to instantiate prefab: " + error);
                if (a.contains("tag")) scene.QueueSetTag(handle, a.value("tag", std::string{}));
                if (a.contains("layer")) scene.QueueSetLayer(handle, a.value("layer", uint32_t{0}));
                if (a.contains("editorFlags")) scene.QueueSetEditorFlags(handle, a.value("editorFlags", uint32_t{0}));
                handles[id] = handle;
                continue;
            }

            ActorCreateDesc desc;
            desc.name = std::move(name);
            desc.persistentID = id;
            desc.activeSelf = active;
            desc.tag = a.value("tag", std::string{});
            desc.layer = a.value("layer", uint32_t{0});
            desc.editorFlags = a.value("editorFlags", uint32_t{0});
            if (a.contains("transform")) desc.transform = TransformFromJson(a["transform"]);
            if (a.contains("components") && a["components"].is_array()) {
                for (const Json& c : a["components"]) {
                    if (!c.is_object()) continue;
                    ComponentCreateDesc component;
                    component.type = c.value("type", std::string{});
                    component.enabled = c.value("enabled", true);
                    component.data = c.contains("data") ? c["data"] : Json::object();
                    if (!component.type.empty()) desc.components.push_back(std::move(component));
                }
            }
            handles[id] = scene.QueueCreateActor(desc);
        }

        // Restore nextID
        if (root.contains("nextID")) {
            scene.SetNextID(static_cast<uint64_t>(root["nextID"]));
        }

        // Pass 2: queue relationships after every handle has been reserved.
        for (const Json& a : actorsArr) {
            if (!a.is_object()) continue;
            uint64_t id       = a.value("id",       uint64_t(0));
            uint64_t parentID = a.value("parentID", uint64_t(0));
            if (parentID != 0) {
                const auto actor = handles.find(id);
                const auto parent = handles.find(parentID);
                if (actor != handles.end() && parent != handles.end())
                    scene.QueueSetParent(actor->second, parent->second);
            }
        }
        if (!scene.FlushCommands()) {
            scene.Clear();
            return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        Logger::Error("SceneSerializer: parse error: ", e.what());
        scene.Clear();
        return false;
    }
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

bool SceneSerializer::SaveToFile(const Scene& scene, const std::string& filepath)
{
    try {
        Json root = SceneToJson(scene);
        std::ofstream f(filepath);
        if (!f.is_open()) {
            Logger::Error("SceneSerializer: cannot open '", filepath, "' for writing");
            return false;
        }
        f << root.dump(4);
        Logger::Info("SceneSerializer: saved '", scene.GetName(),
                     "' → ", filepath,
                     "  (", scene.ActorCount(), " actors)");
        return true;
    }
    catch (const std::exception& e) {
        Logger::Error("SceneSerializer: save error: ", e.what());
        return false;
    }
}

bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& filepath)
{
    try {
        std::ifstream f(filepath);
        if (!f.is_open()) {
            Logger::Error("SceneSerializer: cannot open '", filepath, "' for reading");
            return false;
        }
        Json root = Json::parse(f);
        bool ok   = JsonToScene(root, scene);
        if (ok) {
            Logger::Info("SceneSerializer: loaded '", scene.GetName(),
                         "' ← ", filepath,
                         "  (", scene.ActorCount(), " actors)");
        }
        return ok;
    }
    catch (const std::exception& e) {
        Logger::Error("SceneSerializer: load error: ", e.what());
        return false;
    }
}

std::string SceneSerializer::SaveToString(const Scene& scene)
{
    return SceneToJson(scene).dump(4);
}

bool SceneSerializer::LoadFromString(Scene& scene, const std::string& jsonStr)
{
    try {
        Json root = Json::parse(jsonStr);
        return JsonToScene(root, scene);
    }
    catch (const std::exception& e) {
        Logger::Error("SceneSerializer: parse error: ", e.what());
        return false;
    }
}
