#include "Editor/InspectorSections.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorPanelHelpers.h"
#include "Physics/BoxColliderComponent.h"
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

namespace {
using namespace EditorPanelHelpers;

constexpr const char kTexturePayload[] = "MYENGINE_TEXTURE_PATH";

Actor* SelectedActor(EditorContext& context)
{
    Scene* scene = context.GetScene();
    return scene ? context.GetSelection().ResolveActor(*scene) : nullptr;
}

class ActorInspectorSection : public EditorInspectorSection {
public:
    bool CanDraw(const EditorSelection& selection) const override
    {
        return selection.HasActor();
    }
};

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
        ImGui::TextUnformatted("Transform");
        Transform& transform = actor->GetTransform();
        DrawVec3("Position", transform.position, 0.05f);
        DrawVec3("Rotation", transform.rotation, 0.2f);
        DrawVec3("Scale", transform.scale, 0.05f);
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
        ImGui::TextUnformatted("Mesh Renderer");
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

        if (ImGui::Button("Remove Mesh Renderer")) {
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
        auto* renderer = actor ? actor->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
        if (!renderer) return;

        ImGui::Separator();
        ImGui::PushID("SkinnedMesh");
        ImGui::TextUnformatted("Skinned Mesh");
        DrawEnabled(*renderer);

        bool playing = renderer->IsPlaying();
        if (ImGui::Checkbox("Playing", &playing)) renderer->SetPlaying(playing);
        float weight = renderer->GetBlendWeight();
        if (ImGui::SliderFloat("Blend Weight", &weight, 0.0f, 1.0f)) {
            renderer->SetBlendWeight(weight);
        }
        if (ImGui::Button("Remove Skinned Mesh")) {
            actor->RemoveComponent<SkinnedMeshRendererComponent>();
        }
        ImGui::PopID();
    }
};

class MaterialInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "material"; }
    int GetOrder() const override { return 120; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* renderer = actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
        MaterialHandle material = renderer ? renderer->GetMaterial() : MaterialHandle {};
        if (!material.IsValid()) return;

        const auto before = CloneMaterial(*material);
        bool changed = false;
        ImGui::Separator();
        ImGui::PushID("Material");
        ImGui::TextUnformatted("Material Properties");

        int blendMode = static_cast<int>(material->GetBlendMode());
        const char* blendModes[] = {"Opaque", "AlphaTest", "Transparent"};
        if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 3)) {
            material->SetBlendMode(static_cast<BlendMode>(std::clamp(blendMode, 0, 2)));
            changed = true;
        }

        bool twoSided = material->IsTwoSided();
        if (ImGui::Checkbox("Two Sided", &twoSided)) {
            material->SetTwoSided(twoSided);
            changed = true;
        }

        const MaterialParam baseColor = material->GetParam(
            "BaseColor", MaterialParam::FromVec4(1.0f, 1.0f, 1.0f, 1.0f));
        float color[4] = {
            baseColor.data[0], baseColor.data[1], baseColor.data[2], baseColor.data[3]
        };
        if (ImGui::ColorEdit4("Base Color", color)) {
            material->SetParam("BaseColor", MaterialParam::FromVec4(
                color[0], color[1], color[2], color[3]));
            changed = true;
        }

        float metallic = material->GetFloat("Metallic", 0.0f);
        float roughness = material->GetFloat("Roughness", 0.5f);
        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
            material->SetParam("Metallic", MaterialParam::FromFloat(metallic));
            changed = true;
        }
        if (ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f)) {
            material->SetParam("Roughness", MaterialParam::FromFloat(roughness));
            changed = true;
        }

        const std::pair<const char*, const char*> slots[] = {
            {"Base Color Map", "BaseColorMap"},
            {"Normal Map", "NormalMap"},
            {"Metallic Roughness", "MetallicRoughnessMap"},
            {"Occlusion Map", "OcclusionMap"},
            {"Emissive Map", "EmissiveMap"}
        };
        for (const auto& [label, slot] : slots) {
            const TextureHandle texture = material->GetTexture(slot);
            ImGui::Text("%s: %s", label,
                        texture.IsValid() ? texture->GetPath().c_str() : "(none)");
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTexturePayload)) {
                    const auto loaded = AssetManager::Get().Load<TextureAsset>(
                        static_cast<const char*>(payload->Data));
                    if (loaded.IsValid()) {
                        material->SetTexture(slot, loaded);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (changed && context.GetCommandStack()) {
            const auto after = CloneMaterial(*material);
            material->ReloadFrom(*before);
            context.GetCommandStack()->ExecuteCommand(
                std::make_unique<LambdaEditorCommand>(
                    "Edit Material",
                    [material, after](EditorContext&) {
                        return material.IsValid() && material->ReloadFrom(*after);
                    },
                    [material, before](EditorContext&) {
                        return material.IsValid() && material->ReloadFrom(*before);
                    }),
                context);
        }
        ImGui::PopID();
    }
};

class PhysicsInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "physics"; }
    int GetOrder() const override { return 200; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        if (auto* body = actor->GetComponent<RigidBodyComponent>()) {
            ImGui::Separator();
            ImGui::PushID("RigidBody");
            ImGui::TextUnformatted("Rigid Body");
            float mass = body->GetMass();
            if (ImGui::DragFloat("Mass", &mass, 0.05f, 0.0001f, 10000.0f)) {
                body->SetMass(mass);
            }
            bool gravity = body->UsesGravity();
            if (ImGui::Checkbox("Use Gravity", &gravity)) body->SetUseGravity(gravity);
            if (ImGui::Button("Remove Rigid Body")) actor->RemoveComponent<RigidBodyComponent>();
            ImGui::PopID();
        }

        if (auto* box = actor->GetComponent<BoxColliderComponent>()) {
            ImGui::Separator();
            ImGui::PushID("BoxCollider");
            ImGui::TextUnformatted("Box Collider");
            Vec3 extents = box->GetHalfExtents();
            if (DrawVec3("Half Extents", extents, 0.02f)) box->SetHalfExtents(extents);
            DrawCollider(*box);
            if (ImGui::Button("Remove Box Collider")) {
                actor->RemoveComponent<BoxColliderComponent>();
            }
            ImGui::PopID();
        }

        if (auto* sphere = actor->GetComponent<SphereColliderComponent>()) {
            ImGui::Separator();
            ImGui::PushID("SphereCollider");
            ImGui::TextUnformatted("Sphere Collider");
            float radius = sphere->GetRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.02f, 0.001f, 10000.0f)) {
                sphere->SetRadius(radius);
            }
            if (ImGui::Button("Remove Sphere Collider")) {
                actor->RemoveComponent<SphereColliderComponent>();
            }
            ImGui::PopID();
        }
    }
};

class LightInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "light"; }
    int GetOrder() const override { return 300; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* light = actor ? actor->GetComponent<LightComponent>() : nullptr;
        if (!light) return;

        ImGui::Separator();
        ImGui::PushID("Light");
        ImGui::TextUnformatted("Light");

        int type = static_cast<int>(light->GetLightType());
        const char* types[] = {"Directional", "Point", "Spot"};
        if (ImGui::Combo("Type", &type, types, 3)) {
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
        if (ImGui::Button("Remove Light")) actor->RemoveComponent<LightComponent>();
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
        ImGui::TextUnformatted("Post Process");
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
        if (ImGui::Button("Remove Post Process")) {
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
        ImGui::TextWrapped("%s", script->GetScriptPath().empty()
            ? "(inline)" : script->GetScriptPath().c_str());
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
                ComponentRegistry::Get().Create(type, *actor);
                if (auto* renderer = actor->GetComponent<MeshRendererComponent>()) {
                    if (!renderer->GetMesh()) renderer->SetMesh(AssetManager::Get().GetCubeMesh());
                    if (!renderer->GetMaterial()) {
                        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                    }
                }
            }
            if (exists) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
};
}

std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections()
{
    std::vector<std::unique_ptr<EditorInspectorSection>> sections;
    sections.push_back(std::make_unique<TransformInspectorSection>());
    sections.push_back(std::make_unique<MeshRendererInspectorSection>());
    sections.push_back(std::make_unique<SkinnedMeshInspectorSection>());
    sections.push_back(std::make_unique<MaterialInspectorSection>());
    sections.push_back(std::make_unique<PhysicsInspectorSection>());
    sections.push_back(std::make_unique<LightInspectorSection>());
    sections.push_back(std::make_unique<PostProcessInspectorSection>());
    sections.push_back(std::make_unique<ScriptInspectorSection>());
    sections.push_back(std::make_unique<AddComponentInspectorSection>());
    return sections;
}
