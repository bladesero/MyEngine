#include "Scene/SceneSerializer.h"
#include "Scene/Actor.h"
#include "Scene/Component.h"
#include "Scene/ComponentRegistry.h"
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

    Json actorsArr = Json::array();

    scene.ForEach([&](Actor& actor) {
        Json a;
        a["id"]       = actor.GetID();
        a["name"]     = actor.GetName();
        a["active"]   = actor.IsActive();
        a["parentID"] = actor.GetParent() ? actor.GetParent()->GetID() : uint64_t(0);
        a["transform"] = TransformToJson(actor.GetTransform());

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

        if (!root.contains("actors")) return true; // empty scene is valid

        const Json& actorsArr = root["actors"];
        if (!actorsArr.is_array()) {
            Logger::Warn("SceneSerializer: 'actors' is not an array");
            return true;
        }

        // --- Pass 1: create all actors with their IDs ---
        for (const Json& a : actorsArr) {
            if (!a.is_object()) {
                Logger::Warn("SceneSerializer: skipping malformed actor entry");
                continue;
            }

            uint64_t    id     = a.value("id",     uint64_t(0));
            std::string name   = a.value("name",   std::string("Actor"));
            bool        active = a.value("active", true);

            Actor* actor = scene.CreateActorWithID(name, id);
            actor->SetActive(active);

            if (a.contains("transform")) {
                actor->GetTransform() = TransformFromJson(a["transform"]);
            }
        }

        // Restore nextID
        if (root.contains("nextID")) {
            scene.SetNextID(static_cast<uint64_t>(root["nextID"]));
        }

        // --- Pass 2: wire up parent-child relationships ---
        for (const Json& a : actorsArr) {
            if (!a.is_object()) continue;
            uint64_t id       = a.value("id",       uint64_t(0));
            uint64_t parentID = a.value("parentID", uint64_t(0));
            if (parentID != 0) {
                Actor* actor  = scene.FindByID(id);
                Actor* parent = scene.FindByID(parentID);
                if (actor && parent) {
                    actor->SetParent(parent);
                }
            }
        }

        // --- Pass 3: deserialize components ---
        for (const Json& a : actorsArr) {
            if (!a.is_object()) continue;
            uint64_t id    = a.value("id", uint64_t(0));
            Actor*   actor = scene.FindByID(id);
            if (!actor || !a.contains("components")) continue;
            if (!a["components"].is_array()) {
                Logger::Warn("SceneSerializer: components for actor '",
                             actor->GetName(), "' is not an array");
                continue;
            }

            for (const Json& c : a["components"]) {
                if (!c.is_object()) {
                    Logger::Warn("SceneSerializer: skipping malformed component on actor '",
                                 actor->GetName(), "'");
                    continue;
                }
                std::string typeName = c.value("type", "");
                bool        enabled  = c.value("enabled", true);
                Json emptyObj = Json::object();
                const Json& data = c.contains("data") ? c["data"] : emptyObj;

                Component* compPtr = ComponentRegistry::Get().Create(typeName, *actor);

                if (compPtr) {
                    try {
                        compPtr->SetEnabled(enabled);
                        compPtr->Deserialize(data);
                    }
                    catch (const std::exception& e) {
                        Logger::Error("SceneSerializer: component '", typeName,
                                      "' on actor '", actor->GetName(),
                                      "' failed to deserialize: ", e.what());
                        actor->RemoveComponentByTypeName(typeName);
                    }
                } else {
                    Logger::Warn("SceneSerializer: unregistered component type '",
                                 typeName, "'");
                }
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        Logger::Error("SceneSerializer: parse error: ", e.what());
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
