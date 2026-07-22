#include "Editor/InspectorSectionShared.h"

namespace {
class SceneSettingsInspectorSection final : public EditorInspectorSection {
public:
    const char* GetID() const override { return "sceneSettings"; }
    int GetOrder() const override { return -10; }

    bool CanDraw(const EditorSelectObject& object, const EditorContext&) const override { return object.IsNone(); }

    void Draw(EditorContext& context) override {
        Scene* scene = context.GetScene();
        if (!scene)
            return;
        ImGui::Separator();
        ImGui::TextUnformatted("Scene Settings");

        std::array<char, 128> nameBuf{};
        std::strncpy(nameBuf.data(), scene->GetName().c_str(), nameBuf.size() - 1);
        if (ImGui::InputText("Name", nameBuf.data(), nameBuf.size())) {
            CommitSceneNameEdit(context, scene->GetName(), nameBuf.data());
        }

        ImGui::Text("Actors: %zu", scene->ActorCount());

        std::vector<Actor*> cameraActors;
        scene->ForEach([&](Actor& actor) {
            if (actor.GetComponent<CameraComponent>())
                cameraActors.push_back(&actor);
        });
        const uint64_t currentMainCameraHint = scene->GetMainCameraHintActorID();
        std::string mainCameraLabel = "Auto";
        if (currentMainCameraHint != 0) {
            Actor* hintedActor = scene->FindByID(currentMainCameraHint);
            mainCameraLabel = hintedActor ? hintedActor->GetName() + " (" + std::to_string(currentMainCameraHint) + ")"
                                          : "Missing actor " + std::to_string(currentMainCameraHint);
        }
        if (ImGui::BeginCombo("Main Camera Hint", mainCameraLabel.c_str())) {
            if (ImGui::Selectable("Auto", currentMainCameraHint == 0)) {
                CommitSceneMainCameraHintEdit(context, currentMainCameraHint, 0);
            }
            for (Actor* cameraActor : cameraActors) {
                if (!cameraActor)
                    continue;
                const uint64_t actorID = cameraActor->GetID();
                const std::string label = cameraActor->GetName() + " (" + std::to_string(actorID) + ")";
                if (ImGui::Selectable(label.c_str(), currentMainCameraHint == actorID)) {
                    CommitSceneMainCameraHintEdit(context, currentMainCameraHint, actorID);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Rendering Defaults");
        const SceneEnvironmentData environment = CollectSceneEnvironmentData(*scene);
        float ambientIntensity = scene->GetAmbientIntensity();
        if (environment.HasSkylight())
            ImGui::BeginDisabled();
        if (ImGui::DragFloat("Legacy Ambient Intensity", &ambientIntensity, 0.05f, 0.0f, 20.0f, "%.2f")) {
            CommitSceneAmbientIntensityEdit(context, scene->GetAmbientIntensity(), ambientIntensity);
        }
        if (environment.HasSkylight()) {
            ImGui::EndDisabled();
            Actor* source = scene->FindByID(environment.sourceActorID);
            ImGui::TextDisabled("Controlled by Skylight: %s",
                                source ? source->GetName().c_str() : "missing source actor");
        } else {
            ImGui::TextDisabled("Used when no active Skylight is present");
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Physics");
        Vec3 grav = scene->GetPhysicsWorld().GetGravity();
        if (DrawVec3("Gravity", grav, 0.1f)) {
            CommitSceneGravityEdit(context, scene->GetPhysicsWorld().GetGravity(), grav);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Navigation");
        const NavigationWorld& navigation = scene->GetNavigationWorld();
        ImGui::Text("Status: %s", navigation.IsBaked() ? "Baked" : "Not baked");
        if (navigation.IsBaked()) {
            ImGui::Text("Grid: %u x %u", navigation.GetWidth(), navigation.GetHeight());
        }
        if (ImGui::Button("Bake Navigation")) {
            EditorNavigationBakeService{}.Bake(context, *scene);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Lighting Probes");
        LightingProbeBakeSettings lightingSettings = scene->GetLightingProbeBakeSettings();
        int resolutionIndex = lightingSettings.reflectionResolution == 64    ? 0
                              : lightingSettings.reflectionResolution == 256 ? 2
                                                                             : 1;
        const char* resolutions[] = {"64", "128", "256"};
        bool lightingSettingsChanged = false;
        if (ImGui::Combo("Reflection Resolution", &resolutionIndex, resolutions, 3)) {
            lightingSettings.reflectionResolution = resolutionIndex == 0 ? 64u : resolutionIndex == 2 ? 256u : 128u;
            lightingSettingsChanged = true;
        }
        lightingSettingsChanged |=
            ImGui::DragFloat("RGBM Maximum Range", &lightingSettings.rgbmMaximumRange, 1.0f, 4.0f, 64.0f, "%.0f");
        if (lightingSettingsChanged) {
            CommitLightingProbeBakeSettingsEdit(context, *scene, lightingSettings);
        }
        EditorLightingBakeService* bakeService = context.GetService<EditorLightingBakeService>();
        const bool bakePending = bakeService && bakeService->IsBakePending(*scene);
        const bool bakeCurrent = bakeService && bakeService->IsBakeCurrent(*scene);
        ImGui::Text("Status: %s", bakePending                                  ? "GPU bake queued"
                                  : scene->GetLightingProbeAssetPath().empty() ? "Not baked"
                                  : bakeCurrent                                ? "Baked"
                                                                               : "Stale");
        if (!scene->GetLightingProbeAssetPath().empty())
            ImGui::TextWrapped("Asset: %s", scene->GetLightingProbeAssetPath().c_str());
        ImGui::BeginDisabled(!bakeService || bakePending);
        if (ImGui::Button("Bake All Lighting Probes"))
            bakeService->RequestBake(*scene);
        ImGui::EndDisabled();
    }
};

// ---------------------------------------------------------------------------
// Asset Inspector base (P1-B.2)
// ---------------------------------------------------------------------------
class AssetInspectorSection : public EditorInspectorSection {
public:
    explicit AssetInspectorSection(EditorAssetType type) : m_Type(type) {}

    bool CanDraw(const EditorSelectObject& object, const EditorContext& context) const override {
        if (!object.IsAsset())
            return false;
        const EditorAssetRegistry* registry = context.GetAssetRegistry();
        const EditorAssetInfo* info = registry ? registry->GetAssetInfo(object.GetAssetPath()) : nullptr;
        if (m_Type == EditorAssetType::Unknown) {
            return !info || (info->type != EditorAssetType::Material && info->type != EditorAssetType::Texture &&
                             info->type != EditorAssetType::Model && info->type != EditorAssetType::Prefab &&
                             info->type != EditorAssetType::Audio);
        }
        return info && info->type == m_Type;
    }

private:
    EditorAssetType m_Type;
};

class ModelAssetInspectorSection final : public AssetInspectorSection {
public:
    ModelAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Model) {}
    const char* GetID() const override { return "modelAsset"; }
    int GetOrder() const override { return -7; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        ImGui::Separator();
        ImGui::PushID("ModelAsset");
        DrawAssetMetadataHeader(context, path, "Model");

        auto handle = AssetManager::Get().GetByPath<ModelAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<ModelAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Model not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        const ModelAsset* model = handle.Get();
        ImGui::Separator();
        ImGui::Text("Model: %s", model->GetName().c_str());
        ImGui::Text("Materials: %d", model->MaterialCount());
        ImGui::Text("Nodes: %zu", model->GetNodes().size());
        ImGui::Text("Bones: %zu", model->GetBones().size());
        ImGui::Text("Animations: %zu", model->GetAnimations().size());

        if (const MeshAsset* mesh = model->GetMeshPtr()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Mesh");
            ImGui::Text("Vertices: %u", mesh->VertexCount());
            ImGui::Text("Indices: %u", mesh->IndexCount());
            ImGui::Text("Submeshes: %zu", mesh->GetSubMeshes().size());
            ImGui::Text("LODs: %zu", mesh->GetLods().size());
            ImGui::Text("GPU Uploaded: %s", mesh->IsUploaded() ? "Yes" : "No");
        } else {
            ImGui::TextDisabled("Mesh unavailable");
        }

        if (!model->GetMaterials().empty() && ImGui::TreeNodeEx("Material Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t index = 0; index < model->GetMaterials().size(); ++index) {
                const MaterialHandle& material = model->GetMaterials()[index];
                ImGui::Text("%zu: %s", index, material.IsValid() ? material->GetPath().c_str() : "(missing)");
            }
            ImGui::TreePop();
        }

        if (!model->GetAnimations().empty() && ImGui::TreeNodeEx("Animations")) {
            for (const AnimationClip& clip : model->GetAnimations()) {
                ImGui::Text("%s  %.2fs  tracks=%zu", clip.name.empty() ? "(unnamed)" : clip.name.c_str(), clip.duration,
                            clip.tracks.size());
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
};

class PrefabAssetInspectorSection final : public AssetInspectorSection {
public:
    PrefabAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Prefab) {}
    const char* GetID() const override { return "prefabAsset"; }
    int GetOrder() const override { return -6; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        ImGui::Separator();
        ImGui::PushID("PrefabAsset");
        DrawAssetMetadataHeader(context, path, "Prefab");

        PrefabAsset prefab;
        std::string error;
        if (!PrefabAsset::Load(path, prefab, &error)) {
            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.25f, 1.0f), "Prefab failed to load: %s", error.c_str());
            ImGui::PopID();
            return;
        }

        const PrefabNode* rootNode = nullptr;
        size_t componentCount = 0;
        size_t rootCount = 0;
        size_t sceneInstanceCount = 0;
        const std::filesystem::path resolvedPrefabPath = PrefabSystem::ResolvePrefabPath(path).lexically_normal();
        for (const PrefabNode& node : prefab.nodes) {
            componentCount += node.components.size();
            if (node.parentLocalId.empty())
                ++rootCount;
            if (node.localId == prefab.rootLocalId)
                rootNode = &node;
        }
        if (Scene* scene = context.GetScene()) {
            scene->ForEach([&](Actor& actor) {
                if (!actor.IsPrefabRoot() || actor.GetPrefabAssetPath().empty())
                    return;
                const std::filesystem::path actorPrefabPath =
                    PrefabSystem::ResolvePrefabPath(actor.GetPrefabAssetPath()).lexically_normal();
                if (actorPrefabPath == resolvedPrefabPath)
                    ++sceneInstanceCount;
            });
        }

        ImGui::Separator();
        ImGui::Text("Prefab UUID: %s", prefab.uuid.empty() ? "(none)" : prefab.uuid.c_str());
        ImGui::Text("Root Local ID: %s", prefab.rootLocalId.empty() ? "(none)" : prefab.rootLocalId.c_str());
        ImGui::Text("Root Actor: %s", rootNode ? rootNode->name.c_str() : "(missing)");
        ImGui::Text("Nodes: %zu", prefab.nodes.size());
        ImGui::Text("Root Nodes: %zu", rootCount);
        ImGui::Text("Components: %zu", componentCount);
        ImGui::Text("Scene Instances: %zu", sceneInstanceCount);

        ImGui::BeginDisabled(sceneInstanceCount == 0 || !context.GetOperators());
        if (ImGui::Button("Select Scene Instances")) {
            context.GetOperators()->Prefabs().SelectInstances(context, path);
        }
        ImGui::EndDisabled();

        if (!prefab.Validate(&error)) {
            ImGui::TextColored(ImVec4(0.9f, 0.65f, 0.15f, 1.0f), "Validation: %s", error.c_str());
        }

        if (!prefab.nodes.empty() && ImGui::TreeNodeEx("Prefab Nodes", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const PrefabNode& node : prefab.nodes) {
                ImGui::PushID(node.localId.c_str());
                const std::string label = node.name.empty() ? "(unnamed)###node" : node.name + "###node";
                const bool open =
                    ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth |
                                                         (node.components.empty() ? ImGuiTreeNodeFlags_Leaf : 0));
                ImGui::SameLine();
                ImGui::TextDisabled("%s", node.localId.c_str());
                if (open) {
                    ImGui::Text("Parent: %s", node.parentLocalId.empty() ? "(root)" : node.parentLocalId.c_str());
                    ImGui::Text("Active: %s", node.activeSelf ? "Yes" : "No");
                    if (!node.components.empty() && ImGui::TreeNodeEx("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const ComponentCreateDesc& component : node.components) {
                            ImGui::Text("%s%s", component.type.empty() ? "(unknown component)" : component.type.c_str(),
                                        component.enabled ? "" : " (disabled)");
                        }
                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
};

class MaterialAssetInspectorSection final : public AssetInspectorSection {
public:
    MaterialAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Material) {}
    const char* GetID() const override { return "materialAsset"; }
    int GetOrder() const override { return -5; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        ImGui::Separator();
        ImGui::PushID("MaterialAsset");
        DrawAssetMetadataHeader(context, path, "Material");

        auto handle = AssetManager::Get().GetByPath<MaterialAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<MaterialAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Material not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        auto* mat = handle.Get();
        ImGui::Separator();
        ImGui::Text("Material: %s", mat->GetName().c_str());
        ImGui::Text("Format: v%u%s", mat->GetFormatVersion(),
                    mat->WasLoadedFromLegacyFormat() ? " (legacy; save migrates to v2)" : "");

        const char* shaderLabel =
            mat->GetShaderAsset().IsValid() ? mat->GetShaderAsset()->GetPath().c_str() : "(inherited / none)";
        ImGui::TextWrapped("Shader: %s", shaderLabel);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MYENGINE_ASSET_PATH")) {
                const std::string dropped(static_cast<const char*>(payload->Data));
                if (std::filesystem::path(dropped).extension() == ".shader") {
                    ModifyMaterialAssetField(context, path, "Set Material Shader", [dropped](MaterialAsset& target) {
                        target.SetShaderAsset(AssetManager::Get().Load<ShaderAsset>(dropped));
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextWrapped("Parent: %s", mat->HasParent() ? mat->GetParentPath().c_str() : "(none)");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MYENGINE_ASSET_PATH")) {
                const std::string dropped(static_cast<const char*>(payload->Data));
                if (std::filesystem::path(dropped).extension() == ".mat") {
                    ModifyMaterialAssetField(context, path, "Set Material Parent", [dropped](MaterialAsset& target) {
                        target.SetParentPath(AssetManager::Get().MakeProjectRelativePath(dropped));
                        target.SetSurfaceOverrideMask(0);
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (mat->HasParent()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Parent"))
                ModifyMaterialAssetField(context, path, "Clear Material Parent",
                                         [](MaterialAsset& target) { target.SetParentPath({}); });
        }

        // Blend mode
        int blendMode = static_cast<int>(mat->GetBlendMode());
        const char* blendModes[] = {"Opaque", "AlphaTest", "Transparent"};
        if (ImGui::Combo("Blend Mode", &blendMode, blendModes, 3)) {
            ModifyMaterialAssetField(context, path, "Set Material Blend Mode", [blendMode](MaterialAsset& target) {
                target.SetBlendMode(static_cast<BlendMode>(blendMode));
            });
        }

        // Alpha threshold
        if (mat->GetBlendMode() == BlendMode::AlphaTest) {
            float threshold = mat->GetAlphaThreshold();
            if (ImGui::DragFloat("Alpha Threshold", &threshold, 0.01f, 0.0f, 1.0f)) {
                ModifyMaterialAssetField(context, path, "Set Material Alpha Threshold",
                                         [threshold](MaterialAsset& target) { target.SetAlphaThreshold(threshold); });
            }
        }

        // Two-sided
        bool twoSided = mat->IsTwoSided();
        if (ImGui::Checkbox("Two Sided", &twoSided)) {
            ModifyMaterialAssetField(context, path, "Set Material Two Sided",
                                     [twoSided](MaterialAsset& target) { target.SetTwoSided(twoSided); });
        }

        // Wireframe
        bool wireframe = mat->IsWireframe();
        if (ImGui::Checkbox("Wireframe", &wireframe)) {
            ModifyMaterialAssetField(context, path, "Set Material Wireframe",
                                     [wireframe](MaterialAsset& target) { target.SetWireframe(wireframe); });
        }

        MaterialSystem materialSystem;
        const ResolvedMaterial resolved = materialSystem.Resolve(*mat);
        if (!resolved.valid && (mat->HasParent() || mat->GetShaderAsset().IsValid())) {
            for (const std::string& diagnostic : resolved.diagnostics)
                ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "%s", diagnostic.c_str());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Shader Properties");

        if (resolved.shader.IsValid()) {
            for (const ShaderPropertyDesc& property : resolved.shader->GetProperties()) {
                ImGui::PushID(property.id.c_str());
                const bool legacyLocal = mat->WasLoadedFromLegacyFormat() && mat->HasParam(property.name);
                bool overridden = property.type == ShaderPropertyType::Texture2D
                                      ? mat->HasTexture(property.id) ||
                                            (mat->WasLoadedFromLegacyFormat() && mat->HasTexture(property.name))
                                      : mat->HasParam(property.id) || legacyLocal;
                bool toggled = overridden;
                if (ImGui::Checkbox("##override", &toggled)) {
                    const TextureHandle inheritedTexture = resolved.FindTexture(property.id);
                    const MaterialParam inheritedValue = resolved.FindProperty(property.id)
                                                             ? *resolved.FindProperty(property.id)
                                                             : MaterialSystem::DefaultValue(property);
                    if (property.type == ShaderPropertyType::Texture2D) {
                        ModifyMaterialAssetField(
                            context, path, toggled ? "Enable Material Override" : "Restore Inherited Material Value",
                            [id = property.id, legacyName = property.name, toggled,
                             inheritedTexture](MaterialAsset& target) {
                                if (toggled)
                                    target.SetTexture(id, inheritedTexture);
                                else {
                                    target.RemoveTexture(id);
                                    target.RemoveTexture(legacyName);
                                }
                            });
                    } else {
                        ModifyMaterialAssetField(
                            context, path, toggled ? "Enable Material Override" : "Restore Inherited Material Value",
                            [id = property.id, legacyName = property.name, toggled,
                             inheritedValue](MaterialAsset& target) {
                                if (toggled)
                                    target.SetParam(id, inheritedValue);
                                else {
                                    target.RemoveParam(id);
                                    target.RemoveParam(legacyName);
                                }
                            });
                    }
                }
                ImGui::SameLine();
                if (property.type == ShaderPropertyType::Texture2D) {
                    TextureHandle value =
                        overridden ? mat->GetTexture(mat->HasTexture(property.id) ? property.id : property.name)
                                   : resolved.FindTexture(property.id);
                    ImGui::Text("%s: %s", property.name.c_str(), value.IsValid() ? value->GetPath().c_str() : "(none)");
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTexturePayload)) {
                            const std::string dropped(static_cast<const char*>(payload->Data));
                            ModifyMaterialAssetField(context, path, "Set Material Texture",
                                                     [id = property.id, dropped](MaterialAsset& target) {
                                                         target.SetTexture(
                                                             id, AssetManager::Get().Load<TextureAsset>(dropped));
                                                     });
                        }
                        ImGui::EndDragDropTarget();
                    }
                } else {
                    const MaterialParam* inherited = resolved.FindProperty(property.id);
                    MaterialParam value =
                        overridden ? mat->GetParam(mat->HasParam(property.id) ? property.id : property.name,
                                                   inherited ? *inherited : MaterialSystem::DefaultValue(property))
                                   : (inherited ? *inherited : MaterialSystem::DefaultValue(property));
                    if (!overridden)
                        ImGui::BeginDisabled();
                    bool changed = false;
                    if (property.type == ShaderPropertyType::Float) {
                        changed = property.hasRange ? ImGui::SliderFloat(property.name.c_str(), &value.data[0],
                                                                         property.minValue, property.maxValue)
                                                    : ImGui::DragFloat(property.name.c_str(), &value.data[0], 0.01f);
                    } else if (property.type == ShaderPropertyType::Bool) {
                        bool checked = value.data[0] > 0.5f;
                        changed = ImGui::Checkbox(property.name.c_str(), &checked);
                        value.data[0] = checked ? 1.0f : 0.0f;
                    } else if (property.type == ShaderPropertyType::Vec2) {
                        changed = ImGui::DragFloat2(property.name.c_str(), value.data, 0.01f);
                    } else if (property.type == ShaderPropertyType::Vec3) {
                        changed = ImGui::DragFloat3(property.name.c_str(), value.data, 0.01f);
                    } else if (property.type == ShaderPropertyType::Color) {
                        changed = ImGui::ColorEdit4(property.name.c_str(), value.data);
                    }
                    if (!overridden)
                        ImGui::EndDisabled();
                    if (changed && overridden)
                        ModifyMaterialAssetField(
                            context, path, "Set Material Property",
                            [id = property.id, value](MaterialAsset& target) { target.SetParam(id, value); });
                }
                ImGui::PopID();
            }
        }

        if (!resolved.shader.IsValid()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Legacy Parameters");

            // Material parameters
            std::vector<std::pair<std::string, MaterialParam>> params;
            params.reserve(mat->GetParams().size());
            for (const auto& [name, param] : mat->GetParams()) {
                params.emplace_back(name, param);
            }
            for (const auto& [name, param] : params) {
                ImGui::PushID(name.c_str());
                switch (param.type) {
                case MaterialParam::Type::Float: {
                    float v = param.data[0];
                    if (ImGui::DragFloat(name.c_str(), &v, 0.01f)) {
                        ModifyMaterialAssetField(
                            context, path, "Set Material Parameter",
                            [name, v](MaterialAsset& target) { target.SetParam(name, MaterialParam::FromFloat(v)); });
                    }
                    break;
                }
                case MaterialParam::Type::Vec3: {
                    Vec3 v(param.data[0], param.data[1], param.data[2]);
                    if (DrawVec3(name.c_str(), v, 0.01f)) {
                        ModifyMaterialAssetField(context, path, "Set Material Parameter",
                                                 [name, v](MaterialAsset& target) {
                                                     target.SetParam(name, MaterialParam::FromVec3(v.x, v.y, v.z));
                                                 });
                    }
                    break;
                }
                case MaterialParam::Type::Vec4: {
                    float data[4] = {param.data[0], param.data[1], param.data[2], param.data[3]};
                    if (ImGui::ColorEdit4(name.c_str(), data)) {
                        const float x = data[0];
                        const float y = data[1];
                        const float z = data[2];
                        const float w = data[3];
                        ModifyMaterialAssetField(context, path, "Set Material Parameter",
                                                 [name, x, y, z, w](MaterialAsset& target) {
                                                     target.SetParam(name, MaterialParam::FromVec4(x, y, z, w));
                                                 });
                    }
                    break;
                }
                default:
                    ImGui::Text("%s: (unsupported type)", name.c_str());
                    break;
                }
                ImGui::PopID();
            }
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
            MaterialModifier modifier(path, "Modify Material", [mat](MaterialAsset& target) {
                target.ReloadFrom(*mat);
                return true;
            });
            if (modifier.Modify(context)) {
                Logger::Info("[Editor] Material saved: ", path);
            } else {
                Logger::Warn("[Editor] Failed to save material: ", path);
            }
        }

        ImGui::PopID();
    }
};

class ShaderAssetInspectorSection final : public AssetInspectorSection {
public:
    ShaderAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Shader) {}
    const char* GetID() const override { return "shaderAsset"; }
    int GetOrder() const override { return -5; }
    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        auto shader = LoadShaderAssetFromFile(path);
        DrawAssetMetadataHeader(context, path, "Shader");
        if (!shader) {
            ImGui::TextDisabled("Shader failed to load");
            return;
        }
        ImGui::Text("Mode: %s", shader->IsGraph() ? "Graph" : "Code");
        ImGui::Text("Domain: %s", shader->GetDomain() == ShaderDomain::Surface   ? "Surface"
                                  : shader->GetDomain() == ShaderDomain::Compute ? "Compute"
                                                                                 : "Graphics");
        if (shader->IsGraph())
            ImGui::Text("Surface: %s / %s", shader->GetShadingModel() == ShaderShadingModel::Lit ? "Lit" : "Unlit",
                        shader->GetSurfaceType() == ShaderSurfaceType::Transparent ? "Transparent"
                        : shader->GetSurfaceType() == ShaderSurfaceType::Masked    ? "Masked"
                                                                                   : "Opaque");
        ImGui::SeparatorText("Generated Passes");
        for (uint32_t value = 0; value < static_cast<uint32_t>(ShaderPass::Count); ++value)
            if (shader->HasPass(static_cast<ShaderPass>(value)))
                ImGui::BulletText("%s", ShaderPassName(static_cast<ShaderPass>(value)));
        ImGui::SeparatorText("Properties");
        for (const auto& property : shader->GetProperties())
            ImGui::BulletText("%s  [%s]  id=%s", property.name.c_str(), ShaderPropertyTypeName(property.type),
                              property.id.c_str());
        ImGui::SeparatorText("Diagnostics");
        for (const auto& diagnostic : shader->GetDiagnostics())
            ImGui::TextColored(diagnostic.severity == ShaderGraphDiagnostic::Severity::Error ? ImVec4(1, .3f, .2f, 1)
                                                                                             : ImVec4(1, .8f, .2f, 1),
                               "[%llu] %s", static_cast<unsigned long long>(diagnostic.nodeId),
                               diagnostic.message.c_str());
        if (shader->IsGraph() && ImGui::Button("Open Shader Graph"))
            context.RequestPanelFocus("shaderGraph");
    }
};

class TextureAssetInspectorSection final : public AssetInspectorSection {
public:
    TextureAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Texture) {}
    const char* GetID() const override { return "textureAsset"; }
    int GetOrder() const override { return -4; }

    static const char* FilterName(TextureFilter filter) {
        switch (filter) {
        case TextureFilter::Nearest:
            return "Nearest";
        default:
            return "Linear";
        }
    }

    static const char* WrapName(TextureWrap wrap) {
        switch (wrap) {
        case TextureWrap::Clamp:
            return "Clamp";
        default:
            return "Repeat";
        }
    }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        ImGui::Separator();
        ImGui::PushID("TextureAsset");
        DrawAssetMetadataHeader(context, path, "Texture");

        auto handle = AssetManager::Get().GetByPath<TextureAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<TextureAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Texture not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        auto* tex = handle.Get();
        ImGui::Separator();
        ImGui::Text("Texture: %s", tex->GetName().c_str());

        int w = tex->GetWidth();
        int h = tex->GetHeight();
        ImGui::Text("Size: %d x %d", w, h);
        ImGui::Text("Mips: %d", tex->GetMipLevels());
        ImGui::Text("Format: SRGB=%s", tex->GetDesc().sRGB ? "Yes" : "No");

        TextureFilter filter = tex->GetFilter();
        TextureWrap wrapU = tex->GetWrapU();
        TextureWrap wrapV = tex->GetWrapV();
        bool changed = false;
        ImGui::Separator();
        ImGui::TextUnformatted("Sampler");
        if (ImGui::BeginCombo("Filter", FilterName(filter))) {
            if (ImGui::Selectable("Nearest", filter == TextureFilter::Nearest)) {
                filter = TextureFilter::Nearest;
                changed = true;
            }
            if (ImGui::Selectable("Linear", filter != TextureFilter::Nearest)) {
                filter = TextureFilter::Linear;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Wrap U", WrapName(wrapU))) {
            if (ImGui::Selectable("Repeat", wrapU != TextureWrap::Clamp)) {
                wrapU = TextureWrap::Repeat;
                changed = true;
            }
            if (ImGui::Selectable("Clamp", wrapU == TextureWrap::Clamp)) {
                wrapU = TextureWrap::Clamp;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Wrap V", WrapName(wrapV))) {
            if (ImGui::Selectable("Repeat", wrapV != TextureWrap::Clamp)) {
                wrapV = TextureWrap::Repeat;
                changed = true;
            }
            if (ImGui::Selectable("Clamp", wrapV == TextureWrap::Clamp)) {
                wrapV = TextureWrap::Clamp;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (changed) {
            const EditorAssetRegistry* registry = context.GetAssetRegistry();
            const EditorAssetInfo* info = registry ? registry->GetAssetInfo(path) : nullptr;
            if (info && info->imported && !info->uuid.empty()) {
                const std::string settingsJson =
                    ImportSettingsWithTextureSampler(context, info->uuid, filter, wrapU, wrapV);
                if (auto* operators = context.GetOperators()) {
                    operators->Assets().ReimportWithSettings(context, info->uuid, settingsJson);
                } else {
                    EditorAssetOperator assetOperator;
                    assetOperator.ReimportWithSettings(context, info->uuid, settingsJson);
                }
            }
        }

        // Texture thumbnail (requires ImGui GPU backend integration for preview)\n        if (tex->GetGpuHandle()) {\n
        // ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "(GPU resident)");\n        } else {\n
        // ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(preview not available)");\n        }

        ImGui::PopID();
    }
};

class AudioAssetInspectorSection final : public AssetInspectorSection {
public:
    AudioAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Audio) {}
    const char* GetID() const override { return "audioAsset"; }
    int GetOrder() const override { return -3; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        ImGui::Separator();
        ImGui::PushID("AudioAsset");
        DrawAssetMetadataHeader(context, path, "Audio");

        auto handle = AssetManager::Get().GetByPath<AudioClipAsset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<AudioClipAsset>(path);
        }
        if (!handle.IsValid()) {
            ImGui::TextDisabled("Audio clip not loaded: %s", path.c_str());
            ImGui::PopID();
            return;
        }

        const AudioClipAsset* clip = handle.Get();
        ImGui::Separator();
        ImGui::Text("Clip: %s", clip->GetName().c_str());
        ImGui::Text("Channels: %u", clip->GetChannels());
        ImGui::Text("Sample Rate: %u Hz", clip->GetSampleRate());
        ImGui::Text("Frames: %llu", static_cast<unsigned long long>(clip->GetFrameCount()));
        ImGui::Text("Duration: %.3f seconds", clip->GetDurationSeconds());
        ImGui::PopID();
    }
};

class GenericAssetInspectorSection final : public AssetInspectorSection {
public:
    GenericAssetInspectorSection() : AssetInspectorSection(EditorAssetType::Unknown) {}
    const char* GetID() const override { return "genericAsset"; }
    int GetOrder() const override { return 0; }

    void Draw(EditorContext& context) override {
        const std::string& path = context.GetSelection().GetAssetPath();
        if (path.empty())
            return;

        DrawAssetMetadataHeader(context, path, "Asset");

        if (IsJsonAsset(path)) {
            ImGui::TextUnformatted("Type: JSON");
            DrawJsonAssetPreview(path);
            return;
        }

        auto handle = AssetManager::Get().GetByPath<Asset>(path);
        if (!handle.IsValid()) {
            handle = AssetManager::Get().Load<Asset>(path);
        }

        ImGui::Text("Asset: %s", handle.IsValid() ? handle->GetName().c_str() : "(not loaded)");

        if (handle.IsValid()) {
            ImGui::Text("Type: %s", AssetTypeToString(handle->GetType()));
            ImGui::Text("Loaded: %s", handle->IsReady() ? "Yes" : "No");
        }
    }
};

// ... (rest of existing inspector sections unchanged)
std::shared_ptr<MaterialAsset> CloneMaterial(const MaterialAsset& source) {
    auto result = std::make_shared<MaterialAsset>(source.GetPath());
    result->SetName(source.GetName());
    result->ReloadFrom(source);
    return result;
}

} // namespace

void RegisterAssetSceneInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections) {
    sections.push_back(std::make_unique<SceneSettingsInspectorSection>());
    sections.push_back(std::make_unique<ModelAssetInspectorSection>());
    sections.push_back(std::make_unique<PrefabAssetInspectorSection>());
    sections.push_back(std::make_unique<MaterialAssetInspectorSection>());
    sections.push_back(std::make_unique<ShaderAssetInspectorSection>());
    sections.push_back(std::make_unique<TextureAssetInspectorSection>());
    sections.push_back(std::make_unique<AudioAssetInspectorSection>());
    sections.push_back(std::make_unique<GenericAssetInspectorSection>());
}
