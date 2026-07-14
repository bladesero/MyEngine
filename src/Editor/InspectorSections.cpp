#include "Editor/InspectorSections.h"
#include "Editor/InspectorSectionGroups.h"
#include "Editor/InspectorSectionShared.h"
#include "Scene/TypeRegistry.h"

#include <memory>
#include <vector>

namespace {
class MetadataInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "metadataComponents"; }
    int GetOrder() const override { return 900; }
    void Draw(EditorContext& context) override {
#ifdef MYENGINE_ENABLE_IMGUI
        Actor* actor = SelectedActor(context);
        if (!actor)
            return;
        actor->ForEachComponent([&](Component& component) {
            const TypeDescriptor* type = TypeRegistry::Get().Find(component.GetTypeName());
            if (!type || type->properties.empty() || type->customInspector)
                return;
            ImGui::Separator();
            ImGui::PushID(component.GetTypeName());
            if (!ImGui::CollapsingHeader(type->displayName.empty() ? type->stableName.c_str()
                                                                   : type->displayName.c_str())) {
                ImGui::PopID();
                return;
            }
            for (const PropertyDescriptor& property : type->properties) {
                if (!HasPropertyFlag(property.flags, PropertyFlags::Inspector))
                    continue;
                nlohmann::json oldValue;
                if (!TypeRegistry::Get().GetProperty(component, property.stableName, oldValue))
                    continue;
                nlohmann::json newValue = oldValue;
                bool edited = false;
                const char* label = property.hints.displayName.empty() ? property.stableName.c_str()
                                                                       : property.hints.displayName.c_str();
                if (property.kind == PropertyKind::Bool) {
                    bool v = oldValue.get<bool>();
                    edited = ImGui::Checkbox(label, &v);
                    newValue = v;
                } else if (property.kind == PropertyKind::Float) {
                    float v = oldValue.get<float>();
                    edited = ImGui::DragFloat(
                        label, &v, property.hints.step > 0 ? static_cast<float>(property.hints.step) : 0.05f);
                    newValue = v;
                } else if (property.kind == PropertyKind::Vec3 || property.kind == PropertyKind::Color) {
                    float v[3] = {oldValue[0].get<float>(), oldValue[1].get<float>(), oldValue[2].get<float>()};
                    edited = property.kind == PropertyKind::Color ? ImGui::ColorEdit3(label, v)
                                                                  : ImGui::DragFloat3(label, v, 0.05f);
                    newValue = nlohmann::json::array({v[0], v[1], v[2]});
                }
                if (!edited)
                    continue;
                nlohmann::json before, after;
                uint32_t version = 0;
                TypeRegistry::Get().Serialize(component, before, version, nullptr);
                TypeRegistry::Get().SetProperty(component, property.stableName, newValue, nullptr);
                TypeRegistry::Get().Serialize(component, after, version, nullptr);
                TypeRegistry::Get().Deserialize(component, type->stableName, version, before, nullptr);
                if (auto* operators = context.GetOperators())
                    operators->Components().SetProperty(context, *actor, type->stableName, property.stableName, before,
                                                        after);
            }
            ImGui::PopID();
        });
#else
        (void)context;
#endif
    }
};
} // namespace

std::vector<std::unique_ptr<EditorInspectorSection>> CreateDefaultInspectorSections() {
    std::vector<std::unique_ptr<EditorInspectorSection>> sections;
    RegisterAssetSceneInspectorSections(sections);
    RegisterTransformRenderInspectorSections(sections);
    RegisterGameplayInspectorSections(sections);
    RegisterAudioInspectorSections(sections);
    RegisterPhysicsInspectorSections(sections);
    RegisterScriptingInspectorSections(sections);
    RegisterUIInspectorSections(sections);
    sections.push_back(std::make_unique<MetadataInspectorSection>());
    RegisterAddComponentInspectorSections(sections);
    return sections;
}
