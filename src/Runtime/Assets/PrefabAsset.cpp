#include "Assets/PrefabAsset.h"

#include "Core/RuntimeFileSystem.h"

#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace {
void SetError(std::string* error, const std::string& value) { if (error) *error = value; }
nlohmann::json Vec3Json(const Vec3& value) { return {value.x, value.y, value.z}; }
Vec3 ReadVec3(const nlohmann::json& value, Vec3 fallback = {}) {
    if (!value.is_array() || value.size() < 3) return fallback;
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}
}

nlohmann::json PrefabNodeToJson(const PrefabNode& node)
{
    nlohmann::json components = nlohmann::json::array();
    for (const auto& component : node.components)
        components.push_back({{"type", component.type}, {"enabled", component.enabled}, {"data", component.data}});
    nlohmann::json result = {{"localId", node.localId}, {"parentLocalId", node.parentLocalId},
            {"name", node.name}, {"active", node.activeSelf},
            {"transform", {{"position", Vec3Json(node.transform.position)},
                           {"rotation", Vec3Json(node.transform.rotation)},
                           {"scale", Vec3Json(node.transform.scale)}}},
            {"components", std::move(components)}};
    if (node.editorFlags != 0) result["editorFlags"] = node.editorFlags;
    return result;
}

bool PrefabNodeFromJson(const nlohmann::json& json, PrefabNode& node, std::string* error)
{
    try {
        if (!json.is_object()) { SetError(error, "prefab node is not an object"); return false; }
        node = {};
        node.localId = json.value("localId", std::string{});
        node.parentLocalId = json.value("parentLocalId", std::string{});
        node.name = json.value("name", std::string("Actor"));
        node.activeSelf = json.value("active", true);
        node.editorFlags = json.value("editorFlags", uint32_t{0});
        if (const auto transform = json.find("transform"); transform != json.end() && transform->is_object()) {
            node.transform.position = ReadVec3(transform->value("position", nlohmann::json::array()), node.transform.position);
            node.transform.rotation = ReadVec3(transform->value("rotation", nlohmann::json::array()), node.transform.rotation);
            node.transform.scale = ReadVec3(transform->value("scale", nlohmann::json::array()), node.transform.scale);
        }
        if (const auto components = json.find("components"); components != json.end() && components->is_array()) {
            for (const auto& value : *components) {
                if (!value.is_object()) continue;
                ComponentCreateDesc component;
                component.type = value.value("type", std::string{});
                component.enabled = value.value("enabled", true);
                component.data = value.value("data", nlohmann::json::object());
                if (!component.type.empty()) node.components.push_back(std::move(component));
            }
        }
        if (node.localId.empty()) { SetError(error, "prefab node localId is empty"); return false; }
        return true;
    } catch (const std::exception& exception) { SetError(error, exception.what()); return false; }
}

bool PrefabAsset::Validate(std::string* error) const
{
    if (version != kVersion) { SetError(error, "unsupported prefab version"); return false; }
    if (uuid.empty() || rootLocalId.empty() || nodes.empty()) { SetError(error, "prefab header is incomplete"); return false; }
    std::unordered_set<std::string> ids;
    std::unordered_map<std::string,std::string> parents;
    size_t roots = 0;
    for (const auto& node : nodes) {
        if (node.localId.empty() || !ids.insert(node.localId).second) { SetError(error, "duplicate prefab localId"); return false; }
        if (node.parentLocalId.empty()) ++roots;
        parents[node.localId]=node.parentLocalId;
        std::unordered_set<std::string> componentTypes;
        for(const auto& component:node.components)if(!componentTypes.insert(component.type).second){SetError(error,"duplicate prefab component type");return false;}
    }
    if (roots != 1 || !ids.count(rootLocalId)) { SetError(error, "prefab must contain exactly one root"); return false; }
    for (const auto& node : nodes) if (!node.parentLocalId.empty() && !ids.count(node.parentLocalId)) {
        SetError(error, "prefab parent localId is missing"); return false;
    }
    for(const auto& node:nodes){std::unordered_set<std::string> path;std::string current=node.localId;
        while(!current.empty()){if(!path.insert(current).second){SetError(error,"prefab hierarchy contains a cycle");return false;}current=parents[current];}
        if(!path.count(rootLocalId)){SetError(error,"prefab node is disconnected from root");return false;}
    }
    return true;
}

bool PrefabAsset::Save(const std::filesystem::path& path, std::string* error) const
{
    if (!Validate(error)) return false;
    try {
        nlohmann::json nodeArray = nlohmann::json::array();
        for (const auto& node : nodes) nodeArray.push_back(PrefabNodeToJson(node));
        nlohmann::json root = {{"version", version}, {"uuid", uuid}, {"rootLocalId", rootLocalId}, {"nodes", std::move(nodeArray)}};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::trunc);
        output << root.dump(2) << '\n';
        if (!output) { SetError(error, "failed to write prefab"); return false; }
        return true;
    } catch (const std::exception& exception) { SetError(error, exception.what()); return false; }
}

bool PrefabAsset::Load(const std::filesystem::path& path, PrefabAsset& result, std::string* error)
{
    try {
        std::string text;
        if (!RuntimeFileSystem::Get().ReadText(path.string(), text, error)) {
            SetError(error, "prefab file is missing: " + path.string());
            return false;
        }
        nlohmann::json json = nlohmann::json::parse(text);
        PrefabAsset asset;
        asset.version = json.value("version", 0u);
        asset.uuid = json.value("uuid", std::string{});
        asset.rootLocalId = json.value("rootLocalId", std::string{});
        if (!json.contains("nodes") || !json["nodes"].is_array()) { SetError(error, "prefab nodes are missing"); return false; }
        for (const auto& value : json["nodes"]) {
            PrefabNode node;
            if (!PrefabNodeFromJson(value, node, error)) return false;
            asset.nodes.push_back(std::move(node));
        }
        if (!asset.Validate(error)) return false;
        result = std::move(asset);
        return true;
    } catch (const std::exception& exception) { SetError(error, exception.what()); return false; }
}
