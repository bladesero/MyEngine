#include "Editor/InspectorSections.h"
#include "Editor/InspectorSectionGroups.h"
#include "Editor/InspectorSectionShared.h"
#include "Scene/TypeRegistry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
struct InspectorPropertyGroup {
    std::string name;
    int order = 0;
    size_t firstPropertyIndex = 0;
    std::vector<const PropertyDescriptor*> properties;
};

std::vector<InspectorPropertyGroup> BuildInspectorPropertyGroups(const TypeDescriptor& type) {
    std::vector<InspectorPropertyGroup> groups;
    for (size_t index = 0; index < type.properties.size(); ++index) {
        const PropertyDescriptor& property = type.properties[index];
        if (!HasPropertyFlag(property.flags, PropertyFlags::Inspector))
            continue;

        auto group = std::find_if(groups.begin(), groups.end(), [&](const InspectorPropertyGroup& candidate) {
            return candidate.name == property.hints.group;
        });
        if (group == groups.end()) {
            groups.push_back({property.hints.group, property.hints.order, index, {&property}});
        } else {
            group->order = (std::min)(group->order, property.hints.order);
            group->properties.push_back(&property);
        }
    }

    std::stable_sort(groups.begin(), groups.end(),
                     [](const InspectorPropertyGroup& a, const InspectorPropertyGroup& b) {
                         if (a.order != b.order)
                             return a.order < b.order;
                         return a.firstPropertyIndex < b.firstPropertyIndex;
                     });
    for (InspectorPropertyGroup& group : groups) {
        std::stable_sort(
            group.properties.begin(), group.properties.end(),
            [](const PropertyDescriptor* a, const PropertyDescriptor* b) { return a->hints.order < b->hints.order; });
    }
    return groups;
}

uint32_t UInt32HintBound(double value) {
    const double maximum = static_cast<double>((std::numeric_limits<uint32_t>::max)());
    return static_cast<uint32_t>(std::clamp(value, 0.0, maximum));
}

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
            for (const InspectorPropertyGroup& group : BuildInspectorPropertyGroups(*type)) {
                if (!group.name.empty())
                    ImGui::SeparatorText(group.name.c_str());
                for (const PropertyDescriptor* propertyPtr : group.properties) {
                    const PropertyDescriptor& property = *propertyPtr;
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
                        const float speed = property.hints.step > 0 ? static_cast<float>(property.hints.step) : 0.05f;
                        if (property.hints.hasRange) {
                            float minimum = static_cast<float>(property.hints.minimum);
                            float maximum = static_cast<float>(property.hints.maximum);
                            if (maximum < minimum)
                                std::swap(minimum, maximum);
                            edited = ImGui::DragFloat(label, &v, speed, minimum, maximum);
                            if (edited)
                                v = std::clamp(v, minimum, maximum);
                        } else {
                            edited = ImGui::DragFloat(label, &v, speed);
                        }
                        newValue = v;
                    } else if (property.kind == PropertyKind::UInt32) {
                        uint32_t v = oldValue.get<uint32_t>();
                        const float speed = property.hints.step > 0 ? static_cast<float>(property.hints.step) : 1.0f;
                        if (property.hints.hasRange) {
                            uint32_t minimum = UInt32HintBound(property.hints.minimum);
                            uint32_t maximum = UInt32HintBound(property.hints.maximum);
                            if (maximum < minimum)
                                std::swap(minimum, maximum);
                            edited = ImGui::DragScalar(label, ImGuiDataType_U32, &v, speed, &minimum, &maximum, "%u");
                            if (edited)
                                v = std::clamp(v, minimum, maximum);
                        } else {
                            edited = ImGui::DragScalar(label, ImGuiDataType_U32, &v, speed, nullptr, nullptr, "%u");
                        }
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
                        operators->Components().SetProperty(context, *actor, type->stableName, property.stableName,
                                                            before, after);
                }
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
