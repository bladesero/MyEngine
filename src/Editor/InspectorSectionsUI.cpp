#include "Editor/InspectorSectionShared.h"

namespace {
class UICanvasInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiCanvas"; }
    int GetOrder() const override { return 520; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* canvas = actor ? actor->GetComponent<UICanvasComponent>() : nullptr;
        if (!canvas) return;

        ImGui::Separator();
        ImGui::PushID("UICanvas");
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, "UI Canvas")) {
            ImGui::PopID();
            return;
        }

        bool visible = canvas->IsVisible();
        if (ImGui::Checkbox("Visible", &visible)) {
            CommitComponentEdit(context, *actor, *canvas, "visible", [&] {
                canvas->SetVisible(visible);
            });
        }
        bool interactive = canvas->IsInteractive();
        if (ImGui::Checkbox("Interactive", &interactive)) {
            CommitComponentEdit(context, *actor, *canvas, "interactive", [&] {
                canvas->SetInteractive(interactive);
            });
        }
        int sortOrder = canvas->GetSortOrder();
        if (ImGui::DragInt("Sort Order", &sortOrder, 1.0f)) {
            CommitComponentEdit(context, *actor, *canvas, "sortOrder", [&] {
                canvas->SetSortOrder(sortOrder);
            });
        }
        int inputMode = static_cast<int>(canvas->GetInputMode());
        if (ImGui::Combo("Input Mode", &inputMode, "None\0UI Only\0Game And UI\0")) {
            CommitComponentEdit(context, *actor, *canvas, "inputMode", [&] {
                canvas->SetInputMode(static_cast<UIInputMode>(inputMode));
            });
        }
        int sourceMode = static_cast<int>(canvas->GetSourceMode());
        if (ImGui::Combo("Source Mode", &sourceMode, "Asset Document\0Actor Tree\0")) {
            CommitComponentEdit(context, *actor, *canvas, "sourceMode", [&] {
                canvas->SetSourceMode(static_cast<UICanvasSourceMode>(sourceMode));
            });
        }

        if (canvas->GetSourceMode() == UICanvasSourceMode::AssetDocument) {
            std::array<char, 260> document{};
            std::strncpy(document.data(), canvas->GetDocumentPath().c_str(), document.size() - 1);
            if (ImGui::InputText("Document", document.data(), document.size())) {
                CommitComponentEdit(context, *actor, *canvas, "documentPath", [&] {
                    canvas->SetDocumentPath(document.data());
                });
            }
        } else {
            auto generatedStyles = canvas->GetGeneratedStylePaths();
            std::string firstStyle = generatedStyles.empty() ? std::string{} : generatedStyles.front();
            if (DrawStringField("Generated Style", firstStyle)) {
                CommitComponentEdit(context, *actor, *canvas, "generatedStylePaths", [&] {
                    canvas->SetGeneratedStylePaths(firstStyle.empty()
                        ? std::vector<std::string>{}
                        : std::vector<std::string>{firstStyle});
                });
            }
        }
        if (ImGui::Button("Reload Document")) {
            canvas->Reload();
        }
        ImGui::SameLine();
        if (EditorWidgets::IconButton("RemoveUICanvas", "X", "Remove UI Canvas"))
            RemoveComponentByType(context, *actor, "UICanvas");
        ImGui::PopID();
    }
};

class UIRectTransformInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiRectTransform"; }
    int GetOrder() const override { return 530; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        auto* rect = actor ? actor->GetComponent<UIRectTransformComponent>() : nullptr;
        if (!rect) return;

        ImGui::Separator();
        ImGui::PushID("UIRectTransform");
        if (!SectionHeaderWithIcon(context, EditorIcons::Actor, "UI Rect Transform")) {
            ImGui::PopID();
            return;
        }
        RectTransform next = rect->GetRect();
        bool changed = false;
        changed |= DrawVec2Field("Anchor Min", next.anchorMin);
        changed |= DrawVec2Field("Anchor Max", next.anchorMax);
        changed |= DrawVec2Field("Offset Min", next.offsetMin);
        changed |= DrawVec2Field("Offset Max", next.offsetMax);
        changed |= DrawVec2Field("Pivot", next.pivot);
        if (changed) {
            CommitComponentEdit(context, *actor, *rect, "Rect Transform", [&]() {
                rect->GetRect() = next;
            });
        }
        if (EditorWidgets::IconButton("RemoveUIRectTransform", "X", "Remove UI Rect Transform")) {
            RemoveComponentByType(context, *actor, "UIRectTransform");
        }
        ImGui::PopID();
    }
};

class UIWidgetInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiWidget"; }
    int GetOrder() const override { return 540; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;

        DrawText(context, *actor);
        DrawImage(context, *actor);
        DrawButton(context, *actor);
        DrawSlider(context, *actor);
        DrawProgress(context, *actor);
        DrawScrollView(context, *actor);
    }

private:
    template <typename T>
    bool DrawCommon(EditorContext& context, Actor& actor, T& component, const char* label)
    {
        ImGui::Separator();
        ImGui::PushID(label);
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, label)) {
            ImGui::PopID();
            return false;
        }
        bool changed = false;
        std::string elementId = component.GetElementID();
        std::string className = component.GetClassName();
        changed |= DrawStringField("Element ID", elementId);
        changed |= DrawStringField("Class", className);
        if (changed) {
            CommitComponentEdit(context, actor, component, label, [&]() {
                component.SetElementID(elementId);
                component.SetClassName(className);
            });
        }
        return true;
    }

    void DrawText(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UITextComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Text")) return;
        std::string text = component->text;
        float fontSize = component->fontSize;
        Color color = component->color;
        bool changed = false;
        changed |= DrawStringField("Text", text);
        changed |= ImGui::DragFloat("Font Size", &fontSize, 1.0f, 1.0f, 256.0f);
        changed |= DrawColorField("Color", color);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Text", [&]() {
                component->text = text;
                component->fontSize = fontSize;
                component->color = color;
            });
        }
        ImGui::PopID();
    }

    void DrawImage(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIImageComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Image")) return;
        std::string source = component->source;
        Color tint = component->tint;
        bool changed = DrawStringField("Source", source);
        changed |= DrawColorField("Tint", tint);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Image", [&]() {
                component->source = source;
                component->tint = tint;
            });
        }
        ImGui::PopID();
    }

    void DrawButton(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIButtonComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Button")) return;
        std::string text = component->text;
        bool disabled = component->disabled;
        bool changed = DrawStringField("Text", text);
        changed |= ImGui::Checkbox("Disabled", &disabled);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Button", [&]() {
                component->text = text;
                component->disabled = disabled;
            });
        }
        ImGui::PopID();
    }

    void DrawSlider(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UISliderComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Slider")) return;
        float value = component->value;
        float min = component->min;
        float max = component->max;
        float step = component->step;
        std::string binding = component->dataBinding;
        bool changed = ImGui::DragFloat("Value", &value, 0.01f);
        changed |= ImGui::DragFloat("Min", &min, 0.01f);
        changed |= ImGui::DragFloat("Max", &max, 0.01f);
        changed |= ImGui::DragFloat("Step", &step, 0.01f, 0.001f, 100.0f);
        changed |= DrawStringField("Data Binding", binding);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Slider", [&]() {
                component->value = value;
                component->min = min;
                component->max = max;
                component->step = step;
                component->dataBinding = binding;
            });
        }
        ImGui::PopID();
    }

    void DrawProgress(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIProgressBarComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Progress Bar")) return;
        float value = component->value;
        float max = component->max;
        bool changed = ImGui::DragFloat("Value", &value, 0.01f);
        changed |= ImGui::DragFloat("Max", &max, 0.01f, 0.001f, 100000.0f);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Progress Bar", [&]() {
                component->value = value;
                component->max = max;
            });
        }
        ImGui::PopID();
    }

    void DrawScrollView(EditorContext& context, Actor& actor)
    {
        auto* component = actor.GetComponent<UIScrollViewComponent>();
        if (!component) return;
        if (!DrawCommon(context, actor, *component, "UI Scroll View")) return;
        bool horizontal = component->horizontal;
        bool vertical = component->vertical;
        bool changed = ImGui::Checkbox("Horizontal", &horizontal);
        changed |= ImGui::Checkbox("Vertical", &vertical);
        if (changed) {
            CommitComponentEdit(context, actor, *component, "Scroll View", [&]() {
                component->horizontal = horizontal;
                component->vertical = vertical;
            });
        }
        ImGui::PopID();
    }
};

class UILayoutInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "uiLayout"; }
    int GetOrder() const override { return 550; }

    void Draw(EditorContext& context) override
    {
        Actor* actor = SelectedActor(context);
        if (!actor) return;
        if (auto* layout = actor->GetComponent<UIVerticalLayoutComponent>()) {
            DrawLayout(context, *actor, *layout, "UI Vertical Layout");
        }
        if (auto* layout = actor->GetComponent<UIHorizontalLayoutComponent>()) {
            DrawLayout(context, *actor, *layout, "UI Horizontal Layout");
        }
        if (auto* grid = actor->GetComponent<UIGridLayoutComponent>()) {
            DrawGrid(context, *actor, *grid);
        }
    }

private:
    template <typename T>
    bool DrawLayout(EditorContext& context, Actor& actor, T& layout, const char* label)
    {
        ImGui::Separator();
        ImGui::PushID(label);
        if (!SectionHeaderWithIcon(context, EditorIcons::Input, label)) {
            ImGui::PopID();
            return false;
        }
        float spacing = layout.spacing;
        float padding = layout.padding;
        std::string alignItems = layout.alignItems;
        std::string justifyContent = layout.justifyContent;
        std::string className = layout.GetClassName();
        bool changed = ImGui::DragFloat("Spacing", &spacing, 1.0f, 0.0f, 256.0f);
        changed |= ImGui::DragFloat("Padding", &padding, 1.0f, 0.0f, 256.0f);
        changed |= DrawStringField("Align Items", alignItems);
        changed |= DrawStringField("Justify Content", justifyContent);
        changed |= DrawStringField("Class", className);
        if (changed) {
            CommitComponentEdit(context, actor, layout, label, [&]() {
                layout.spacing = spacing;
                layout.padding = padding;
                layout.alignItems = alignItems;
                layout.justifyContent = justifyContent;
                layout.SetClassName(className);
            });
        }
        ImGui::PopID();
        return true;
    }

    void DrawGrid(EditorContext& context, Actor& actor, UIGridLayoutComponent& grid)
    {
        if (!DrawLayout(context, actor, grid, "UI Grid Layout")) return;
        int columns = grid.columns;
        int rows = grid.rows;
        float gap = grid.gap;
        bool changed = ImGui::DragInt("Columns", &columns, 1.0f, 1, 64);
        changed |= ImGui::DragInt("Rows", &rows, 1.0f, 1, 64);
        changed |= ImGui::DragFloat("Gap", &gap, 1.0f, 0.0f, 256.0f);
        if (changed) {
            CommitComponentEdit(context, actor, grid, "Grid", [&]() {
                grid.columns = columns;
                grid.rows = rows;
                grid.gap = gap;
            });
        }
    }
};


} // namespace

void RegisterUIInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections)
{
    sections.push_back(std::make_unique<UICanvasInspectorSection>());
    sections.push_back(std::make_unique<UIRectTransformInspectorSection>());
    sections.push_back(std::make_unique<UIWidgetInspectorSection>());
    sections.push_back(std::make_unique<UILayoutInspectorSection>());
}
