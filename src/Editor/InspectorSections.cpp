#include "Editor/InspectorSections.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Audio/AudioSourceComponent.h"
#include "Assets/Asset.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ScriptAsset.h"
#include "Assets/TextureAsset.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorPanelHelpers.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/UI/EditorWidgets.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace {
using namespace EditorPanelHelpers;
namespace EditorWidgets = Editor::UI::EditorWidgets;

constexpr const char kTexturePayload[] = "MYENGINE_TEXTURE_PATH";

const char* ScriptFieldTypeLabel(ScriptFieldType type)
{
    switch (type) {
        case ScriptFieldType::Bool: return "bool";
        case ScriptFieldType::Int: return "int";
        case ScriptFieldType::UInt: return "uint";
        case ScriptFieldType::Float: return "float";
        case ScriptFieldType::Double: return "double";
        case ScriptFieldType::String: return "string";
        case ScriptFieldType::Vec2: return "Vec2";
        case ScriptFieldType::Vec3: return "Vec3";
        default: return "unsupported";
    }
}

Actor* SelectedActor(EditorContext& context)
{
    Scene* scene = context.GetScene();
    return scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
}

bool IsJsonAsset(const std::string& path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".json";
}

void DrawJsonAssetPreview(const std::string& path)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    try {
        std::ifstream input(path);
        if (!input) {
            ImGui::TextDisabled("Unable to open JSON file");
            return;
        }

        nlohmann::json document;
        input >> document;
        std::string preview = document.dump(2);
        constexpr size_t maxPreviewBytes = 64 * 1024;
        if (preview.size() > maxPreviewBytes) {
            preview.resize(maxPreviewBytes);
            preview += "\n... preview truncated ...";
        }

        ImGui::Separator();
        ImGui::TextUnformatted("JSON Preview");
        ImGui::BeginChild("##JsonAssetPreview", ImVec2(0.0f, 0.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::EndChild();
    } catch (const std::exception& exception) {
        ImGui::TextDisabled("Invalid JSON: %s", exception.what());
    }
#else
    (void)path;
#endif
}

bool DrawScriptFieldValue(const ScriptFieldInfo& field, const nlohmann::json& current,
                          nlohmann::json& outValue)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    switch (field.type) {
        case ScriptFieldType::Bool: {
            bool value = current.is_boolean() ? current.get<bool>() : false;
            if (ImGui::Checkbox(field.name.c_str(), &value)) {
                outValue = value;
                return true;
            }
            return false;
        }
        case ScriptFieldType::Int: {
            int value = current.is_number_integer() ? current.get<int>() : 0;
            if (ImGui::DragInt(field.name.c_str(), &value, 1.0f)) {
                outValue = value;
                return true;
            }
            return false;
        }
        case ScriptFieldType::UInt: {
            int value = current.is_number_integer() ? static_cast<int>(current.get<unsigned int>()) : 0;
            if (ImGui::DragInt(field.name.c_str(), &value, 1.0f, 0)) {
                outValue = static_cast<unsigned int>(std::max(0, value));
                return true;
            }
            return false;
        }
        case ScriptFieldType::Float: {
            float value = current.is_number() ? current.get<float>() : 0.0f;
            if (ImGui::DragFloat(field.name.c_str(), &value, 0.05f)) {
                outValue = value;
                return true;
            }
            return false;
        }
        case ScriptFieldType::Double: {
            float value = current.is_number() ? static_cast<float>(current.get<double>()) : 0.0f;
            if (ImGui::DragFloat(field.name.c_str(), &value, 0.05f)) {
                outValue = static_cast<double>(value);
                return true;
            }
            return false;
        }
        case ScriptFieldType::String: {
            std::array<char, 256> buffer{};
            if (current.is_string()) {
                std::strncpy(buffer.data(), current.get<std::string>().c_str(), buffer.size() - 1);
            }
            if (ImGui::InputText(field.name.c_str(), buffer.data(), buffer.size())) {
                outValue = std::string(buffer.data());
                return true;
            }
            return false;
        }
        case ScriptFieldType::Vec2: {
            float value[2] = {};
            if (current.is_array() && current.size() >= 2) {
                value[0] = current[0].get<float>();
                value[1] = current[1].get<float>();
            }
            if (ImGui::DragFloat2(field.name.c_str(), value, 0.05f)) {
                outValue = nlohmann::json::array({ value[0], value[1] });
                return true;
            }
            return false;
        }
        case ScriptFieldType::Vec3: {
            float value[3] = {};
            if (current.is_array() && current.size() >= 3) {
                value[0] = current[0].get<float>();
                value[1] = current[1].get<float>();
                value[2] = current[2].get<float>();
            }
            if (ImGui::DragFloat3(field.name.c_str(), value, 0.05f)) {
                outValue = nlohmann::json::array({ value[0], value[1], value[2] });
                return true;
            }
            return false;
        }
        default:
            ImGui::TextDisabled("%s (%s)", field.name.c_str(), ScriptFieldTypeLabel(field.type));
            return false;
    }
#else
    (void)field;
    (void)current;
    (void)outValue;
    return false;
#endif
}

class ActorInspectorSection : public EditorInspectorSection {
public:
    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext&) const override
    {
        return object.IsActor();
    }
};

// ---------------------------------------------------------------------------
// Scene Settings Inspector (P1-A.2)
// ---------------------------------------------------------------------------
class SceneSettingsInspectorSection final : public EditorInspectorSection {
public:
    const char* GetID() const override { return "sceneSettings"; }
    int GetOrder() const override { return -10; }

    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext&) const override {
        return object.IsNone();
    }

    void Draw(EditorContext& context) override {
        Scene* scene = context.GetScene();
        if (!scene) return;
        ImGui::Separator();
        ImGui::TextUnformatted("Scene Settings");

        std::array<char, 128> nameBuf{};
        std::strncpy(nameBuf.data(), scene->GetName().c_str(), nameBuf.size() - 1);
        if (ImGui::InputText("Name", nameBuf.data(), nameBuf.size())) {
            scene->SetName(nameBuf.data());
            context.MarkSceneDirty();
        }

        ImGui::Text("Actors: %zu", scene->ActorCount());

        ImGui::Separator();
        ImGui::TextUnformatted("Physics");
        Vec3 grav = scene->GetPhysicsWorld().GetGravity();
        if (DrawVec3("Gravity", grav, 0.1f)) {
            scene->GetPhysicsWorld().SetGravity(grav);
            context.MarkSceneDirty();
        }
    }
};

// ---------------------------------------------------------------------------
// Asset Inspector base (P1-B.2)
// ---------------------------------------------------------------------------
class AssetInspectorSection : public EditorInspectorSection {
public:
    explicit AssetInspectorSection(EditorAssetType type) : m_Type(type) {}

    bool CanDraw(const EditorSelectObject& object,
                 const EditorContext& context) const override {
        if (!object.IsAsset()) return false;
        const EditorAssetRegistry* registry = context.GetAssetRegistry();
        const EditorAssetInfo* info = registry
            ? registry->GetAssetInfo(object.GetAssetPath()) : nullptr;
        if (m_Type == EditorAssetType::Unknown) {
            return !info || (info->type != EditorAssetType::Material
                && info->type != EditorAssetType::Texture);
        }
        return info && info->type == m_Type;
    }

private:
    EditorAssetType m_Type;
};

class MaterialAssetInspectorSection final : public AssetInspectorSection {
public:
    MaterialAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Material) {}
    const char* GetID() const override { return "materialAsset"; }
    int GetOrder() const override { return -5; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        auto handle = AssetManager::Get().GetByPath<MaterialAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<MaterialAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Material not loaded: %s", path.c_str());
            return;
        }

        auto* mat = handle.Get();
        ImGui::Separator();
        ImGui::PushID("MaterialAsset");
        ImGui::Text("Material: %s", mat->GetName().c_str());
        ImGui::Text("Path: %s", path.c_str());

        // Blend mode
        int blendMode = static_cast<int>(mat->GetBlendMode());
        const char* blendModes[] = {"Opaque", "AlphaTest", "Transparent"};
        if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 3)) {
            mat->SetBlendMode(static_cast<BlendMode>(blendMode));
        }

        // Alpha threshold
        if (mat->GetBlendMode() == BlendMode::AlphaTest) {
            float threshold = mat->GetAlphaThreshold();
            if (ImGui::DragFloat("Alpha Threshold", &threshold, 0.01f, 0.0f, 1.0f)) {
                mat->SetAlphaThreshold(threshold);
            }
        }

        // Two-sided
        bool twoSided = mat->IsTwoSided();
        if (ImGui::Checkbox("Two Sided", &twoSided)) mat->SetTwoSided(twoSided);

        // Wireframe
        bool wireframe = mat->IsWireframe();
        if (ImGui::Checkbox("Wireframe", &wireframe)) mat->SetWireframe(wireframe);

        ImGui::Separator();
        ImGui::TextUnformatted("Parameters");

        // Material parameters
        const auto& params = mat->GetParams();
        for (const auto& [name, param] : params) {
            ImGui::PushID(name.c_str());
            switch (param.type) {
                case MaterialParam::Type::Float: {
                    float v = param.data[0];
                    if (ImGui::DragFloat(name.c_str(), &v, 0.01f)) {
                        mat->SetParam(name, MaterialParam::FromFloat(v));
                    }
                    break;
                }
                case MaterialParam::Type::Vec3: {
                    Vec3 v(param.data[0], param.data[1], param.data[2]);
                    if (DrawVec3(name.c_str(), v, 0.01f)) {
                        mat->SetParam(name, MaterialParam::FromVec3(v.x, v.y, v.z));
                    }
                    break;
                }
                case MaterialParam::Type::Vec4: {
                    float data[4] = {param.data[0], param.data[1], param.data[2], param.data[3]};
                    if (ImGui::ColorEdit4(name.c_str(), data)) {
                        mat->SetParam(name, MaterialParam::FromVec4(data[0], data[1], data[2], data[3]));
                    }
                    break;
                }
                default: ImGui::Text("%s: (unsupported type)", name.c_str()); break;
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Textures");

        // Textures
        const auto& textures = mat->GetTextures();
        for (const auto& [slot, tex] : textures) {
            ImGui::Text("%s: %s", slot.c_str(), tex.IsValid() ? tex->GetPath().c_str() : "(none)");
        }

        // Save button
        ImGui::Separator();
        if (ImGui::Button("Save Material")) {
            if (SaveMaterialAssetToFile(*mat, path)) {
                Logger::Info("[Editor] Material saved: ", path);
            } else {
                Logger::Warn("[Editor] Failed to save material: ", path);
            }
        }

        ImGui::PopID();
    }
};

class TextureAssetInspectorSection final : public AssetInspectorSection {
public:
    TextureAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Texture) {}
    const char* GetID() const override { return "textureAsset"; }
    int GetOrder() const override { return -4; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        auto handle = AssetManager::Get().GetByPath<TextureAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<TextureAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Texture not loaded: %s", path.c_str());
            return;
        }

        auto* tex = handle.Get();
        ImGui::Separator();
        ImGui::PushID("TextureAsset");
        ImGui::Text("Texture: %s", tex->GetName().c_str());
        ImGui::Text("Path: %s", path.c_str());

        int w = tex->GetWidth();
        int h = tex->GetHeight();
        ImGui::Text("Size: %d x %d", w, h);
        ImGui::Text("Mips: %d", tex->GetMipLevels());
        ImGui::Text("Format: SRGB=%s", tex->GetDesc().sRGB ? "Yes" : "No");

        // Texture thumbnail (requires ImGui GPU backend integration for preview)\n        if (tex->GetGpuHandle()) {\n            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "(GPU resident)");\n        } else {\n            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(preview not available)");\n        }

        ImGui::PopID();
    }
};

class GenericAssetInspectorSection final : public AssetInspectorSection {
public:
    GenericAssetInspectorSection()
        : AssetInspectorSection(EditorAssetType::Unknown) {}
    const char* GetID() const override { return "genericAsset"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty()) return;

        if (IsJsonAsset(path)) {
            ImGui::Separator();
            ImGui::Text("Asset: %s", std::filesystem::path(path).filename().string().c_str());
            ImGui::Text("Path: %s", path.c_str());
            ImGui::TextUnformatted("Type: JSON");
            DrawJsonAssetPreview(path);
            return;
        }

        auto handle = AssetManager::Get().GetByPath<Asset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<Asset>(path);
        }

        ImGui::Separator();
        ImGui::Text("Asset: %s",
            handle.IsValid() ? handle->GetName().c_str() : "(not loaded)");
        ImGui::Text("Path: %s", path.c_str());

        if (handle.IsValid()) {
            ImGui::Text("Type: %s", AssetTypeToString(handle->GetType()));
            ImGui::Text("Loaded: %s", handle->IsReady() ? "Yes" : "No");
        }
    }
};

// ... (rest of existing inspector sections unchanged)
std::shared_ptr<MaterialAsset> CloneMaterial(const MaterialAsset& source)
{
    auto result = std::make_shared<MaterialAsset>(source.GetPath());
    result->SetName(source.GetName());
    result->SetBlendMode(source.GetBlendMode());
    result->SetTwoSided(source.IsTwoSided());
    result->SetWireframe(source.IsWireframe());
    result->SetAlphaThreshold(source.GetAlphaThreshold());
    for (const auto& [name, value] : source.GetParams()) result->SetParam(name, value);
    for (const auto& [slot, texture] : source.GetTextures()) result->SetTexture(slot, texture);
    result->MarkReady();
    return result;
}

class TransformInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "transform"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        ImGui::Separator();
        if (!EditorWidgets::SectionHeader("Transform")) return;
        Transform& transform = actor->GetTransform();
        EditorWidgets::BeginPropertyGrid("TransformProperties");
        if (EditorWidgets::BeginPropertyRow("Position")) {
            float value[3] = {transform.position.x, transform.position.y, transform.position.z};
            if (ImGui::DragFloat3("##Value", value, 0.05f)) {
                transform.position = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        if (EditorWidgets::BeginPropertyRow("Rotation")) {
            float value[3] = {transform.rotation.x, transform.rotation.y, transform.rotation.z};
            if (ImGui::DragFloat3("##Value", value, 0.2f)) {
                transform.rotation = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        if (EditorWidgets::BeginPropertyRow("Scale")) {
            float value[3] = {transform.scale.x, transform.scale.y, transform.scale.z};
            if (ImGui::DragFloat3("##Value", value, 0.05f)) {
                transform.scale = {value[0], value[1], value[2]};
            }
            EditorWidgets::EndPropertyRow();
        }
        EditorWidgets::EndPropertyGrid();
    }
};

class MeshRendererInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "meshRenderer"; }
    int GetOrder() const override { return 100; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* renderer = actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
        if (!renderer) return;

        ImGui::Separator();
        ImGui::PushID("MeshRenderer");
        if (!EditorWidgets::SectionHeader("Mesh Renderer")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*renderer);

        std::vector<std::string> meshes {
            "__builtin__/Triangle", "__builtin__/Quad", "__builtin__/Cube"
        };
        auto extra = AssetManager::Get().GetCachedPathsByType(AssetType::Mesh);
        meshes.insert(meshes.end(), extra.begin(), extra.end());
        const std::string meshPath = renderer->GetMesh() ? renderer->GetMesh()->GetPath() : "";
        if (ImGui::BeginCombo("Mesh", meshPath.empty() ? "(none)" : meshPath.c_str())) {
            for (const std::string& path : meshes) {
                if (ImGui::Selectable(path.c_str(), path == meshPath)) {
                    const MeshHandle mesh = ResolveMesh(path);
                    if (mesh.IsValid()) renderer->SetMesh(mesh);
                }
            }
            ImGui::EndCombo();
        }

        std::vector<std::string> materials {"__builtin__/Default"};
        extra = AssetManager::Get().GetCachedPathsByType(AssetType::Material);
        materials.insert(materials.end(), extra.begin(), extra.end());
        const std::string materialPath = renderer->GetMaterial()
            ? renderer->GetMaterial()->GetPath() : "";
        if (ImGui::BeginCombo("Material", materialPath.empty() ? "(none)" : materialPath.c_str())) {
            for (const std::string& path : materials) {
                if (ImGui::Selectable(path.c_str(), path == materialPath)) {
                    const MaterialHandle material = ResolveMaterial(path);
                    if (material.IsValid()) renderer->SetMaterial(material);
                }
            }
            ImGui::EndCombo();
        }

        if (EditorWidgets::IconButton("RemoveMeshRenderer", "X", "Remove Mesh Renderer")) {
            actor->RemoveComponent<MeshRendererComponent>();
        }
        ImGui::PopID();
    }
};

class SkinnedMeshInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "skinnedMesh"; }
    int GetOrder() const override { return 110; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* skinned = actor ? actor->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
        if (!skinned) return;

        ImGui::Separator();
        ImGui::PushID("SkinnedMesh");
        if (!EditorWidgets::SectionHeader("Skinned Mesh Renderer")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*skinned);

        if (EditorWidgets::IconButton("RemoveSkinnedMesh", "X", "Remove Skinned Mesh")) {
            actor->RemoveComponent<SkinnedMeshRendererComponent>();
        }
        ImGui::PopID();
    }
};

class MaterialInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "material"; }
    int GetOrder() const override { return 200; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* renderer = actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
        if (!renderer || !renderer->GetMaterial().IsValid()) return;

        ImGui::Separator();
        ImGui::PushID("Material");
        if (!EditorWidgets::SectionHeader("Material Instance")) {
            ImGui::PopID();
            return;
        }
        auto* mat = renderer->GetMaterial().Get();
        if (!mat) { ImGui::PopID(); return; }

        ImGui::Text("Source: %s", mat->GetPath().c_str());
        ImGui::PopID();
    }
};

class AudioSourceInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "audioSource"; }
    int GetOrder() const override { return 250; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* source = actor ? actor->GetComponent<AudioSourceComponent>() : nullptr;
        if (!source) return;

        ImGui::Separator();
        ImGui::PushID("AudioSource");
        if (!EditorWidgets::SectionHeader("Audio Source")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*source);

        std::vector<std::string> clips = AssetManager::Get().GetCachedPathsByType(AssetType::AudioClip);
        if (const EditorAssetRegistry* registry = context.GetAssetRegistry()) {
            for (const auto& asset : registry->GetAssets(EditorAssetType::Audio)) {
                clips.push_back(asset.absolutePath.string());
                if (!asset.relativePath.empty())
                    clips.push_back((std::filesystem::path("Content") / asset.relativePath).generic_string());
            }
        }
        std::sort(clips.begin(), clips.end());
        clips.erase(std::unique(clips.begin(), clips.end()), clips.end());

        const std::string current = source->GetClipPath().empty()
            ? std::string("(none)") : source->GetClipPath();
        if (ImGui::BeginCombo("Clip", current.c_str())) {
            if (ImGui::Selectable("(none)", source->GetClipPath().empty()))
                source->SetClipPath({});
            for (const std::string& path : clips)
                if (ImGui::Selectable(path.c_str(), path == source->GetClipPath()))
                    source->SetClipPath(path);
            ImGui::EndCombo();
        }

        bool playOnStart = source->GetPlayOnStart();
        if (ImGui::Checkbox("Play On Start", &playOnStart)) source->SetPlayOnStart(playOnStart);
        bool loop = source->GetLoop();
        if (ImGui::Checkbox("Loop", &loop)) source->SetLoop(loop);
        bool spatial = source->GetSpatial();
        if (ImGui::Checkbox("Spatial", &spatial)) source->SetSpatial(spatial);
        float volume = source->GetVolume();
        if (ImGui::SliderFloat("Volume", &volume, 0.0f, 2.0f)) source->SetVolume(volume);
        float pitch = source->GetPitch();
        if (ImGui::SliderFloat("Pitch", &pitch, 0.25f, 4.0f)) source->SetPitch(pitch);
        float minDistance = source->GetMinDistance();
        if (ImGui::DragFloat("Min Distance", &minDistance, 0.05f, 0.01f, 1000.0f))
            source->SetMinDistance(minDistance);
        float maxDistance = source->GetMaxDistance();
        if (ImGui::DragFloat("Max Distance", &maxDistance, 0.1f, 0.01f, 10000.0f))
            source->SetMaxDistance(maxDistance);

        if (EditorWidgets::IconButton("RemoveAudioSource", "X", "Remove Audio Source"))
            actor->RemoveComponent<AudioSourceComponent>();
        ImGui::PopID();
    }
};

class PhysicsInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "physics"; }
    int GetOrder() const override { return 300; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        auto* rb = actor->GetComponent<RigidBodyComponent>();
        auto* box = actor->GetComponent<BoxColliderComponent>();
        auto* sphere = actor->GetComponent<SphereColliderComponent>();
        auto* capsule = actor->GetComponent<CapsuleColliderComponent>();
        auto* character = actor->GetComponent<CharacterControllerComponent>();
        if (!rb && !box && !sphere && !capsule && !character) return;

        ImGui::Separator();
        ImGui::PushID("Physics");
        if (!EditorWidgets::SectionHeader("Physics")) {
            ImGui::PopID();
            return;
        }
        bool changed = false;

        if (rb) {
            DrawEnabled(*rb);
            int bodyType = static_cast<int>(rb->GetBodyType());
            if (ImGui::Combo("Body Type", &bodyType, "Static\0Dynamic\0Kinematic\0")) {
                rb->SetBodyType(static_cast<BodyType>(bodyType)); changed = true;
            }
            float mass = rb->GetMass();
            if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.01f, 1000.0f)) { rb->SetMass(mass); changed = true; }
            float linearDamping = rb->GetLinearDamping(), angularDamping = rb->GetAngularDamping();
            if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f)) { rb->SetLinearDamping(linearDamping); changed = true; }
            if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f)) { rb->SetAngularDamping(angularDamping); changed = true; }
            float friction = rb->GetFriction(), restitution = rb->GetRestitution();
            if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f)) { rb->SetFriction(friction); changed = true; }
            if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) { rb->SetRestitution(restitution); changed = true; }
            bool gravity = rb->UsesGravity();
            if (ImGui::Checkbox("Use Gravity", &gravity)) { rb->SetUseGravity(gravity); changed = true; }
            bool continuous = rb->GetCollisionDetectionMode() == CollisionDetectionMode::Continuous;
            if (ImGui::Checkbox("Continuous Collision", &continuous)) {
                rb->SetCollisionDetectionMode(continuous ? CollisionDetectionMode::Continuous : CollisionDetectionMode::Discrete);
                changed = true;
            }
            Vec3 velocity = rb->GetVelocity(), angularVelocity = rb->GetAngularVelocity();
            if (DrawVec3("Velocity", velocity, 0.05f)) { rb->SetVelocity(velocity); changed = true; }
            if (DrawVec3("Angular Velocity", angularVelocity, 0.05f)) { rb->SetAngularVelocity(angularVelocity); changed = true; }
            Vec3 linearLocks = rb->GetLinearAxisLocks(), angularLocks = rb->GetAngularAxisLocks();
            if (DrawVec3("Linear Axis Locks", linearLocks, 1.0f)) { rb->SetLinearAxisLocks(linearLocks); changed = true; }
            if (DrawVec3("Angular Axis Locks", angularLocks, 1.0f)) { rb->SetAngularAxisLocks(angularLocks); changed = true; }
            if (EditorWidgets::IconButton("RemoveRigidBody", "X", "Remove RigidBody"))
                actor->RemoveComponent<RigidBodyComponent>();
        }

        const auto drawCollider = [&](ColliderComponent& collider) {
            bool trigger = collider.IsTrigger();
            if (ImGui::Checkbox("Trigger", &trigger)) { collider.SetTrigger(trigger); changed = true; }
            uint32_t layer = collider.GetLayer(), mask = collider.GetLayerMask();
            if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer)) { collider.SetLayer(layer); changed = true; }
            if (ImGui::InputScalar("Layer Mask", ImGuiDataType_U32, &mask)) { collider.SetLayerMask(mask); changed = true; }
        };

        if (box) {
            ImGui::TextUnformatted("Box Collider");
            DrawEnabled(*box);
            Vec3 half = box->GetHalfExtents();
            if (DrawVec3("HalfExtents", half, 0.05f)) { box->SetHalfExtents(half); changed = true; }
            drawCollider(*box);
            if (EditorWidgets::IconButton("RemoveBoxCollider", "X", "Remove Box Collider"))
                actor->RemoveComponent<BoxColliderComponent>();
        }

        if (sphere) {
            ImGui::TextUnformatted("Sphere Collider");
            DrawEnabled(*sphere);
            float radius = sphere->GetRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.01f, 100.0f)) { sphere->SetRadius(radius); changed = true; }
            drawCollider(*sphere);
            if (EditorWidgets::IconButton("RemoveSphereCollider", "X", "Remove Sphere Collider"))
                actor->RemoveComponent<SphereColliderComponent>();
        }
        if (capsule) {
            ImGui::TextUnformatted("Capsule Collider"); DrawEnabled(*capsule);
            float radius = capsule->GetRadius(), halfHeight = capsule->GetHalfHeight();
            if (ImGui::DragFloat("Capsule Radius", &radius, 0.05f, 0.01f, 100.0f)) { capsule->SetRadius(radius); changed = true; }
            if (ImGui::DragFloat("Capsule Half Height", &halfHeight, 0.05f, 0.0f, 100.0f)) { capsule->SetHalfHeight(halfHeight); changed = true; }
            drawCollider(*capsule);
            if (EditorWidgets::IconButton("RemoveCapsuleCollider", "X", "Remove Capsule Collider"))
                actor->RemoveComponent<CapsuleColliderComponent>();
        }
        if (character) {
            ImGui::TextUnformatted("Character Controller"); DrawEnabled(*character);
            bool gravity = character->UsesGravity(); float step = character->GetStepOffset(), slope = character->GetMaxSlopeAngle();
            if (ImGui::Checkbox("Character Gravity", &gravity)) { character->SetUseGravity(gravity); changed = true; }
            if (ImGui::DragFloat("Step Offset", &step, 0.01f, 0.0f, 10.0f)) { character->SetStepOffset(step); changed = true; }
            if (ImGui::SliderFloat("Max Slope Angle", &slope, 0.0f, 89.0f)) { character->SetMaxSlopeAngle(slope); changed = true; }
            if (EditorWidgets::IconButton("RemoveCharacterController", "X", "Remove Character Controller"))
                actor->RemoveComponent<CharacterControllerComponent>();
        }
        if (changed) context.MarkSceneDirty();
        ImGui::PopID();
    }
};

class LightInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "light"; }
    int GetOrder() const override { return 350; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* light = actor ? actor->GetComponent<LightComponent>() : nullptr;
        if (!light) return;

        ImGui::Separator();
        ImGui::PushID("Light");
        if (!EditorWidgets::SectionHeader("Light")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*light);

        int type = static_cast<int>(light->GetLightType());
        if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
            light->SetLightType(static_cast<LightType>(type));
        }
        Vec3 color = light->GetColor();
        float values[3] = {color.x, color.y, color.z};
        if (ImGui::ColorEdit3("Color", values)) {
            light->SetColor({values[0], values[1], values[2]});
        }
        float intensity = light->GetIntensity();
        if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
            light->SetIntensity(intensity);
        }
        Vec3 direction = light->GetDirection();
        if (DrawVec3("Direction", direction, 0.02f)) light->SetDirection(direction);
        if (EditorWidgets::IconButton("RemoveLight", "X", "Remove Light"))
            actor->RemoveComponent<LightComponent>();
        ImGui::PopID();
    }
};

class PostProcessInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "postProcess"; }
    int GetOrder() const override { return 400; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* post = actor ? actor->GetComponent<PostProcessComponent>() : nullptr;
        if (!post) return;

        ImGui::Separator();
        ImGui::PushID("PostProcess");
        if (!EditorWidgets::SectionHeader("Post Process")) {
            ImGui::PopID();
            return;
        }
        float exposure = post->GetExposure();
        float gamma = post->GetGamma();
        float ssao = post->GetSSAOIntensity();
        float bloom = post->GetBloomIntensity();
        if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.0f, 16.0f)) {
            post->SetExposure(exposure);
        }
        if (ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.1f, 8.0f)) post->SetGamma(gamma);
        if (ImGui::DragFloat("SSAO", &ssao, 0.02f, 0.0f, 4.0f)) {
            post->SetSSAOIntensity(ssao);
        }
        if (ImGui::DragFloat("Bloom", &bloom, 0.02f, 0.0f, 8.0f)) {
            post->SetBloomIntensity(bloom);
        }
        if (EditorWidgets::IconButton("RemovePostProcess", "X", "Remove Post Process")) {
            actor->RemoveComponent<PostProcessComponent>();
        }
        ImGui::PopID();
    }
};

class ScriptInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "script"; }
    int GetOrder() const override { return 500; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* script = actor ? actor->GetComponent<ScriptComponent>() : nullptr;
        if (!script) return;

        ImGui::Separator();
        ImGui::PushID("Script");
        ImGui::Text("Script: %s", script->IsCompiled() ? "Compiled" : "Error");
        ImGui::Text("Language: %s", script->GetLanguage().c_str());
        ImGui::Text("Class: %s", script->GetClassName().c_str());
        ImGui::TextWrapped("%s", script->GetScriptPath().empty()
            ? "(inline)" : script->GetScriptPath().c_str());
        if (!script->GetLastError().empty()) {
            ImGui::TextWrapped("Error: %s", script->GetLastError().c_str());
        }
        const auto& fields = script->GetFields();
        if (!fields.empty()) {
            ImGui::TextUnformatted("Parameters");
            const nlohmann::json properties = script->GetProperties();
            for (const auto& field : fields) {
                ImGui::PushID(field.name.c_str());
                const nlohmann::json current = properties.contains(field.name)
                    ? properties[field.name] : field.defaultValue;
                nlohmann::json next;
                if (DrawScriptFieldValue(field, current, next)) {
                    script->SetPropertyValue(field.name, std::move(next));
                    context.MarkSceneDirty();
                }
                if (!field.declaration.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", field.declaration.c_str());
                }
                ImGui::PopID();
            }
        }
        if (ImGui::Button("Remove Script")) actor->RemoveComponent<ScriptComponent>();
        ImGui::PopID();
    }
};

class AddComponentInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "addComponent"; }
    int GetOrder() const override { return 1000; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        ImGui::Separator();
        if (!ImGui::BeginCombo("##AddComponent", "Add Component...")) return;

        for (const std::string& type : ComponentRegistry::Get().GetRegisteredTypes()) {
            const bool exists = actor->HasComponentType(type);
            if (exists) ImGui::BeginDisabled();
            if (ImGui::Selectable(type.c_str()) && !exists) {
                if (auto* stack = context.GetCommandStack()) {
                    stack->ExecuteCommand(std::make_unique<AddComponentCommand>(
                        actor->GetID(), type, nlohmann::json::object()), context);
                } else {
                    ComponentRegistry::Get().Create(type, *actor);
                }
                if (auto* renderer = actor->GetComponent<MeshRendererComponent>()) {
                    if (!renderer->GetMesh()) renderer->SetMesh(AssetManager::Get().GetCubeMesh());
                    if (!renderer->GetMaterial()) {
                        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                    }
                }
            }
            if (exists) ImGui::EndDisabled();
        }
        if (auto* registry = context.GetAssetRegistry()) {
            const bool hasScript = actor->HasComponent<ScriptComponent>();
            const auto scripts = registry->GetAssets(EditorAssetType::Script);
            if (!scripts.empty() && ImGui::BeginMenu("Scripts")) {
                for (const auto& scriptInfo : scripts) {
                    if (scriptInfo.absolutePath.extension() != ".as") continue;
                    auto scriptAsset = AssetManager::Get().Load<ScriptAsset>(scriptInfo.absolutePath.string());
                    if (!scriptAsset || !scriptAsset.Get()) continue;
                    const std::string scriptLabel = scriptInfo.absolutePath.stem().string();
                    if (ImGui::BeginMenu(scriptLabel.c_str())) {
                        for (const auto& scriptClass : scriptAsset->GetClasses()) {
                            if (hasScript) ImGui::BeginDisabled();
                            if (ImGui::Selectable(scriptClass.name.c_str()) && !hasScript) {
                                nlohmann::json properties = nlohmann::json::object();
                                for (const auto& field : scriptClass.fields) {
                                    properties[field.name] = field.defaultValue;
                                }
                                nlohmann::json initialData = {
                                    {"language", "angelscript"},
                                    {"scriptPath", AssetManager::Get().MakeProjectRelativePath(scriptInfo.absolutePath.string())},
                                    {"className", scriptClass.name},
                                    {"properties", properties},
                                    {"state", nlohmann::json::object()}
                                };
                                if (auto* stack = context.GetCommandStack()) {
                                    stack->ExecuteCommand(std::make_unique<AddComponentCommand>(
                                        actor->GetID(), "Script", initialData), context);
                                } else if (auto* component = actor->AddComponent<ScriptComponent>()) {
                                    component->Deserialize(initialData);
                                }
                            }
                            if (hasScript) ImGui::EndDisabled();
                        }
                        if (!scriptAsset->GetLastError().empty()) {
                            ImGui::TextDisabled("%s", scriptAsset->GetLastError().c_str());
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndCombo();
    }
};
} // namespace

std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections()
{
    std::vector<std::unique_ptr<EditorInspectorSection>> sections;
    sections.push_back(std::make_unique<SceneSettingsInspectorSection>());
    sections.push_back(std::make_unique<MaterialAssetInspectorSection>());
    sections.push_back(std::make_unique<TextureAssetInspectorSection>());
    sections.push_back(std::make_unique<GenericAssetInspectorSection>());
    sections.push_back(std::make_unique<TransformInspectorSection>());
    sections.push_back(std::make_unique<MeshRendererInspectorSection>());
    sections.push_back(std::make_unique<SkinnedMeshInspectorSection>());
    sections.push_back(std::make_unique<MaterialInspectorSection>());
    sections.push_back(std::make_unique<AudioSourceInspectorSection>());
    sections.push_back(std::make_unique<PhysicsInspectorSection>());
    sections.push_back(std::make_unique<LightInspectorSection>());
    sections.push_back(std::make_unique<PostProcessInspectorSection>());
    sections.push_back(std::make_unique<ScriptInspectorSection>());
    sections.push_back(std::make_unique<AddComponentInspectorSection>());
    return sections;
}
