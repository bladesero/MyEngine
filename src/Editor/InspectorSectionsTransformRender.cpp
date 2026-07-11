#include "Editor/InspectorSectionShared.h"

namespace {
class TransformInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "transform"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        ImGui::Separator();
        if (!SectionHeaderWithIcon(context, EditorIcons::Actor, "Transform")) return;
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Mesh, "Mesh Renderer")) {
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
        const std::string materialPath = renderer->GetMaterialForSlot(0)
            ? renderer->GetMaterialForSlot(0)->GetPath() : "";
        if (ImGui::BeginCombo("Default Slot / Slot 0",
                              materialPath.empty() ? "(none)" : materialPath.c_str())) {
            for (const std::string& path : materials) {
                if (ImGui::Selectable(path.c_str(), path == materialPath)) {
                    const MaterialHandle material = ResolveMaterial(path);
                    if (material.IsValid()) renderer->SetMaterialSlot(0, material);
                }
            }
            ImGui::EndCombo();
        }

        size_t slotCount = renderer->GetMaterials().size();
        if (MeshAsset* mesh = renderer->GetMesh().Get()) {
            for (const SubMesh& subMesh : mesh->GetSubMeshes()) {
                if (subMesh.materialSlot >= 0) {
                    slotCount = (std::max)(slotCount,
                        static_cast<size_t>(subMesh.materialSlot) + 1);
                }
            }
        }
        if (slotCount > 1) {
            ImGui::TextDisabled("Material Slots");
            for (size_t slot = 0; slot < slotCount; ++slot) {
                ImGui::PushID(static_cast<int>(slot));
                const MaterialHandle current = renderer->GetMaterialForSlot(
                    static_cast<int>(slot));
                const std::string currentPath = current ? current->GetPath() : "";
                const std::string label = "Slot " + std::to_string(slot);
                if (ImGui::BeginCombo(label.c_str(),
                                      currentPath.empty() ? "(none)" : currentPath.c_str())) {
                    for (const std::string& path : materials) {
                        if (ImGui::Selectable(path.c_str(), path == currentPath)) {
                            const MaterialHandle material = ResolveMaterial(path);
                            if (material.IsValid()) {
                                renderer->SetMaterialSlot(slot, material);
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        }

        if (EditorWidgets::IconButton("RemoveMeshRenderer", "X", "Remove Mesh Renderer")) {
            RemoveComponentByType(context, *actor, "MeshRenderer");
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Mesh, "Skinned Mesh Renderer")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*skinned);

        if (EditorWidgets::IconButton("RemoveSkinnedMesh", "X", "Remove Skinned Mesh")) {
            RemoveComponentByType(context, *actor, "SkinnedMeshRenderer");
        }
        ImGui::PopID();
    }
};

class AnimatorInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "animator"; }
    int GetOrder() const override { return 120; }
    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* animator = actor ? actor->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator) return;
        ImGui::Separator();
        ImGui::PushID("Animator");
        if (SectionHeaderWithIcon(context, EditorIcons::Mesh, "Animator")) {
            DrawEnabled(*animator);
            ImGui::Text("State: %s", animator->GetCurrentState().empty() ? "(none)" : animator->GetCurrentState().c_str());
            ImGui::Text("Normalized Time: %.3f", animator->GetNormalizedTime());
            bool rootMotion = animator->AppliesRootMotion();
            if (ImGui::Checkbox("Apply Root Motion", &rootMotion)) {
                CommitComponentEdit(context, *actor, *animator, "applyRootMotion", [&] {
                    animator->SetApplyRootMotion(rootMotion);
                });
            }
            ImGui::TextDisabled("Controller states are serialized with this component in P0.");
            if (EditorWidgets::IconButton("RemoveAnimator", "X", "Remove Animator"))
                RemoveComponentByType(context, *actor, "Animator");
        }
        ImGui::PopID();
    }
};

class ThirdPersonCameraInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "thirdPersonCamera"; }
    int GetOrder() const override { return 121; }
    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* camera = actor ? actor->GetComponent<ThirdPersonCameraComponent>() : nullptr;
        if (!camera) return;
        ImGui::Separator();
        ImGui::PushID("ThirdPersonCamera");
        if (SectionHeaderWithIcon(context, EditorIcons::Camera, "Third Person Camera")) {
            DrawEnabled(*camera);
            float distance = camera->GetDistance();
            float sensitivity = camera->GetSensitivity();
            float radius = camera->GetCollisionRadius();
            float sharpness = camera->GetFollowSharpness();
            Vec3 offset = camera->GetTargetOffset();
            if (ImGui::DragFloat("Distance", &distance, 0.05f, 0.1f, 100.0f))
                CommitComponentEdit(context, *actor, *camera, "distance", [&] { camera->SetDistance(distance); });
            if (DrawVec3("Target Offset", offset, 0.02f))
                CommitComponentEdit(context, *actor, *camera, "targetOffset", [&] { camera->SetTargetOffset(offset); });
            if (ImGui::DragFloat("Sensitivity", &sensitivity, 0.01f, 0.0f, 10.0f))
                CommitComponentEdit(context, *actor, *camera, "sensitivity", [&] { camera->SetSensitivity(sensitivity); });
            if (ImGui::DragFloat("Collision Radius", &radius, 0.01f, 0.0f, 5.0f))
                CommitComponentEdit(context, *actor, *camera, "collisionRadius", [&] { camera->SetCollisionRadius(radius); });
            if (ImGui::DragFloat("Follow Sharpness", &sharpness, 0.1f, 0.0f, 100.0f))
                CommitComponentEdit(context, *actor, *camera, "followSharpness", [&] { camera->SetFollowSharpness(sharpness); });
            if (EditorWidgets::IconButton("RemoveThirdPersonCamera", "X", "Remove Third Person Camera"))
                RemoveComponentByType(context, *actor, "ThirdPersonCamera");
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Material, "Material Instance")) {
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Audio, "Audio Source")) {
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
            RemoveComponentByType(context, *actor, "AudioSource");
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Light, "Light")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*light);

        int type = static_cast<int>(light->GetLightType());
        if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
            CommitComponentEdit(context, *actor, *light, "type", [&] {
                light->SetLightType(static_cast<LightType>(type));
            });
        }
        Vec3 color = light->GetColor();
        float values[3] = {color.x, color.y, color.z};
        if (ImGui::ColorEdit3("Color", values)) {
            CommitComponentEdit(context, *actor, *light, "color", [&] {
                light->SetColor({values[0], values[1], values[2]});
            });
        }
        float intensity = light->GetIntensity();
        if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
            CommitComponentEdit(context, *actor, *light, "intensity", [&] {
                light->SetIntensity(intensity);
            });
        }
        bool castShadows = light->CastsShadows();
        if (ImGui::Checkbox("Cast Shadows", &castShadows)) {
            CommitComponentEdit(context, *actor, *light, "castShadows", [&] {
                light->SetCastShadows(castShadows);
            });
        }
        float shadowIntensity = light->GetShadowIntensity();
        if (ImGui::SliderFloat("Shadow Intensity", &shadowIntensity, 0.0f, 1.0f)) {
            CommitComponentEdit(context, *actor, *light, "shadowIntensity", [&] {
                light->SetShadowIntensity(shadowIntensity);
            });
        }
        Vec3 direction = light->GetDirection();
        if (DrawVec3("Direction", direction, 0.02f)) {
            CommitComponentEdit(context, *actor, *light, "direction", [&] {
                light->SetDirection(direction);
            });
        }
        if (EditorWidgets::IconButton("RemoveLight", "X", "Remove Light"))
            RemoveComponentByType(context, *actor, "Light");
        ImGui::PopID();
    }
};

class CameraInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "camera"; }
    int GetOrder() const override { return 340; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* camera = actor ? actor->GetComponent<CameraComponent>() : nullptr;
        if (!camera) return;

        ImGui::Separator();
        ImGui::PushID("Camera");
        if (!SectionHeaderWithIcon(context, EditorIcons::Camera, "Camera")) {
            ImGui::PopID();
            return;
        }
        DrawEnabled(*camera);

        bool isMain = camera->IsMainCamera();
        if (ImGui::Checkbox("Main Camera", &isMain)) {
            CommitComponentEdit(context, *actor, *camera, "isMainCamera", [&] {
                camera->SetMainCamera(isMain);
            });
        }
        float fov = camera->GetFovYDegrees();
        if (ImGui::DragFloat("FOV Y", &fov, 0.25f, 1.0f, 179.0f)) {
            CommitComponentEdit(context, *actor, *camera, "fovYDegrees", [&] {
                camera->SetFovYDegrees(fov);
            });
        }
        float nearClip = camera->GetNearClip();
        if (ImGui::DragFloat("Near", &nearClip, 0.01f, 0.001f, 1000.0f)) {
            CommitComponentEdit(context, *actor, *camera, "nearClip", [&] {
                camera->SetNearClip(nearClip);
            });
        }
        float farClip = camera->GetFarClip();
        if (ImGui::DragFloat("Far", &farClip, 1.0f, 0.002f, 100000.0f)) {
            CommitComponentEdit(context, *actor, *camera, "farClip", [&] {
                camera->SetFarClip(farClip);
            });
        }
        Vec3 clear = camera->GetClearColor();
        float color[3] = {clear.x, clear.y, clear.z};
        if (ImGui::ColorEdit3("Clear Color", color)) {
            CommitComponentEdit(context, *actor, *camera, "clearColor", [&] {
                camera->SetClearColor({color[0], color[1], color[2]});
            });
        }
        if (EditorWidgets::IconButton("RemoveCamera", "X", "Remove Camera"))
            RemoveComponentByType(context, *actor, "Camera");
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
        if (!SectionHeaderWithIcon(context, EditorIcons::Renderer, "Post Process")) {
            ImGui::PopID();
            return;
        }
        float exposure = post->GetExposure();
        float gamma = post->GetGamma();
        float ssao = post->GetSSAOIntensity();
        float bloom = post->GetBloomIntensity();
        if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.0f, 16.0f)) {
            CommitComponentEdit(context, *actor, *post, "exposure", [&] {
                post->SetExposure(exposure);
            });
        }
        if (ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.1f, 8.0f)) {
            CommitComponentEdit(context, *actor, *post, "gamma", [&] {
                post->SetGamma(gamma);
            });
        }
        if (ImGui::DragFloat("SSAO", &ssao, 0.02f, 0.0f, 4.0f)) {
            CommitComponentEdit(context, *actor, *post, "ssaoIntensity", [&] {
                post->SetSSAOIntensity(ssao);
            });
        }
        if (ImGui::DragFloat("Bloom", &bloom, 0.02f, 0.0f, 8.0f)) {
            CommitComponentEdit(context, *actor, *post, "bloomIntensity", [&] {
                post->SetBloomIntensity(bloom);
            });
        }
        if (EditorWidgets::IconButton("RemovePostProcess", "X", "Remove Post Process")) {
            RemoveComponentByType(context, *actor, "PostProcess");
        }
        ImGui::PopID();
    }
};


} // namespace

void RegisterTransformRenderInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<TransformInspectorSection>());
    sections.push_back(std::make_unique<MeshRendererInspectorSection>());
    sections.push_back(std::make_unique<SkinnedMeshInspectorSection>());
    sections.push_back(std::make_unique<AnimatorInspectorSection>());
    sections.push_back(std::make_unique<ThirdPersonCameraInspectorSection>());
    sections.push_back(std::make_unique<MaterialInspectorSection>());
    sections.push_back(std::make_unique<CameraInspectorSection>());
    sections.push_back(std::make_unique<LightInspectorSection>());
    sections.push_back(std::make_unique<PostProcessInspectorSection>());
}

void RegisterAudioInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<AudioSourceInspectorSection>());
}
