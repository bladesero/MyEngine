#include "Editor/InspectorSectionShared.h"

namespace {
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
                    nlohmann::json before = nlohmann::json::object();
                    script->Serialize(before);
                    script->SetPropertyValue(field.name, next);
                    nlohmann::json after = nlohmann::json::object();
                    script->Serialize(after);
                    script->Deserialize(before);
                    if (auto* operators = context.GetOperators()) {
                        operators->Components().SetProperty(
                            context, *actor, "Script", field.name, before, after);
                    } else {
                        EditorComponentOperator componentOperator;
                        componentOperator.SetProperty(
                            context, *actor, "Script", field.name, before, after);
                    }
                }
                if (!field.declaration.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", field.declaration.c_str());
                }
                ImGui::PopID();
            }
        }
        if (ImGui::Button("Remove Script")) RemoveComponentByType(context, *actor, "Script");
        ImGui::PopID();
    }
};


} // namespace

void RegisterScriptingInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<ScriptInspectorSection>());
}
