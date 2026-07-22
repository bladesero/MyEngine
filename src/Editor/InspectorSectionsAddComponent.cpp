#include "Editor/InspectorSectionShared.h"

#include "Editor/UI/EditorViewportPolicy.h"

namespace {
class AddComponentInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "addComponent"; }
    int GetOrder() const override { return 1000; }

    void Draw(EditorContext& context) override {
        LoadRecentComponents(context);
        Actor* actor = SelectedActor(context);
        if (!actor)
            return;

        ImGui::Separator();
        Editor::UI::EditorViewportPolicy::BindNextPopupToCurrentViewport();
        if (!ImGui::BeginCombo("##AddComponent", "Add Component..."))
            return;

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AddComponentSearch", "Search components...", m_ComponentSearch,
                                 sizeof(m_ComponentSearch));
        ImGui::Separator();

        std::vector<std::string> types = ComponentRegistry::Get().GetRegisteredTypes();
        std::sort(types.begin(), types.end(), [](const std::string& left, const std::string& right) {
            const std::string leftCategory = ComponentCategory(left);
            const std::string rightCategory = ComponentCategory(right);
            if (leftCategory != rightCategory)
                return leftCategory < rightCategory;
            return ComponentDisplayName(left) < ComponentDisplayName(right);
        });

        auto addRegisteredComponent = [&](const std::string& type) {
            Scene* scene = context.GetScene();
            const bool exists =
                actor->HasComponentType(type) || (type == "Skylight" && scene && SceneHasComponentType(*scene, type));
            if (exists)
                ImGui::BeginDisabled();
            const std::string label = ComponentDisplayName(type);
            if (ImGui::Selectable(label.c_str()) && !exists) {
                AddComponentByType(context, *actor, type, nlohmann::json::object());
                RecordRecentComponent(context, type);
                if (auto* renderer = actor->GetComponent<MeshRendererComponent>()) {
                    if (!renderer->GetMesh())
                        renderer->SetMesh(AssetManager::Get().GetCubeMesh());
                    if (!renderer->GetMaterial()) {
                        renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", type.c_str());
            }
            if (exists)
                ImGui::EndDisabled();
        };

        if (!m_RecentComponents.empty()) {
            if (ImGui::BeginMenu("Recently Used")) {
                for (const std::string& type : m_RecentComponents) {
                    if (!ComponentRegistry::Get().IsRegistered(type) ||
                        !ComponentMatchesFilter(type, m_ComponentSearch)) {
                        continue;
                    }
                    addRegisteredComponent(type);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
        }

        const std::vector<std::string> categories = OrderedComponentCategories(types);
        for (const std::string& category : categories) {
            bool hasVisible = false;
            for (const std::string& type : types) {
                if (ComponentCategory(type) == category && ComponentMatchesFilter(type, m_ComponentSearch)) {
                    hasVisible = true;
                    break;
                }
            }
            if (!hasVisible)
                continue;
            if (!ImGui::BeginMenu(category.c_str()))
                continue;
            for (const std::string& type : types) {
                if (ComponentCategory(type) != category || !ComponentMatchesFilter(type, m_ComponentSearch)) {
                    continue;
                }
                addRegisteredComponent(type);
            }
            ImGui::EndMenu();
        }

        if (auto* registry = context.GetAssetRegistry()) {
            const bool hasScript = actor->HasComponent<ScriptComponent>();
            const auto scripts = registry->GetAssets(EditorAssetType::Script);
            if (!scripts.empty() && ImGui::BeginMenu("Scripts")) {
                for (const auto& scriptInfo : scripts) {
                    if (scriptInfo.absolutePath.extension() != ".as")
                        continue;
                    auto scriptAsset = AssetManager::Get().Load<ScriptAsset>(scriptInfo.absolutePath.string());
                    if (!scriptAsset || !scriptAsset.Get())
                        continue;
                    const std::string scriptLabel = scriptInfo.absolutePath.stem().string();
                    const bool scriptFileMatches = ContainsCaseInsensitive(scriptLabel, m_ComponentSearch) ||
                                                   ContainsCaseInsensitive(scriptInfo.relativePath, m_ComponentSearch);
                    bool hasMatchingClass = false;
                    for (const auto& scriptClass : scriptAsset->GetClasses()) {
                        if (scriptFileMatches || ContainsCaseInsensitive(scriptClass.name, m_ComponentSearch)) {
                            hasMatchingClass = true;
                            break;
                        }
                    }
                    if (!hasMatchingClass)
                        continue;
                    if (ImGui::BeginMenu(scriptLabel.c_str())) {
                        for (const auto& scriptClass : scriptAsset->GetClasses()) {
                            if (!scriptFileMatches && !ContainsCaseInsensitive(scriptClass.name, m_ComponentSearch)) {
                                continue;
                            }
                            if (hasScript)
                                ImGui::BeginDisabled();
                            if (ImGui::Selectable(scriptClass.name.c_str()) && !hasScript) {
                                nlohmann::json properties = nlohmann::json::object();
                                for (const auto& field : scriptClass.fields) {
                                    properties[field.name] = field.defaultValue;
                                }
                                nlohmann::json initialData = {
                                    {"language", "angelscript"},
                                    {"scriptPath",
                                     AssetManager::Get().MakeProjectRelativePath(scriptInfo.absolutePath.string())},
                                    {"className", scriptClass.name},
                                    {"properties", properties},
                                    {"state", nlohmann::json::object()}};
                                AddComponentByType(context, *actor, "Script", initialData);
                                RecordRecentComponent(context, "Script");
                            }
                            if (hasScript)
                                ImGui::EndDisabled();
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

private:
    static constexpr const char* kInspectorPanelStateID = "inspector";
    static constexpr const char* kRecentComponentsKey = "addComponentRecent";

    void LoadRecentComponents(EditorContext& context) {
        if (m_RecentComponentsLoaded)
            return;
        m_RecentComponentsLoaded = true;
        EditorWorkspace* workspace = context.GetWorkspace();
        if (!workspace)
            return;
        const auto value = workspace->GetPanelStateValue(kInspectorPanelStateID, kRecentComponentsKey);
        if (!value)
            return;

        size_t start = 0;
        while (start <= value->size()) {
            const size_t separator = value->find(';', start);
            std::string type =
                value->substr(start, separator == std::string::npos ? std::string::npos : separator - start);
            if (!type.empty() && ComponentRegistry::Get().IsRegistered(type) &&
                std::find(m_RecentComponents.begin(), m_RecentComponents.end(), type) == m_RecentComponents.end()) {
                m_RecentComponents.push_back(std::move(type));
            }
            if (separator == std::string::npos || m_RecentComponents.size() >= 5)
                break;
            start = separator + 1;
        }
    }

    void SaveRecentComponents(EditorContext& context) const {
        EditorWorkspace* workspace = context.GetWorkspace();
        if (!workspace)
            return;
        std::string serialized;
        for (const std::string& type : m_RecentComponents) {
            if (!serialized.empty())
                serialized += ';';
            serialized += type;
        }
        workspace->SetPanelStateValue(kInspectorPanelStateID, kRecentComponentsKey, std::move(serialized));
    }

    void RecordRecentComponent(EditorContext& context, const std::string& type) {
        m_RecentComponents.erase(std::remove(m_RecentComponents.begin(), m_RecentComponents.end(), type),
                                 m_RecentComponents.end());
        m_RecentComponents.insert(m_RecentComponents.begin(), type);
        if (m_RecentComponents.size() > 5)
            m_RecentComponents.resize(5);
        SaveRecentComponents(context);
    }

    char m_ComponentSearch[128] = {};
    bool m_RecentComponentsLoaded = false;
    std::vector<std::string> m_RecentComponents;
};

} // namespace

void RegisterAddComponentInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections) {
    sections.push_back(std::make_unique<AddComponentInspectorSection>());
}
