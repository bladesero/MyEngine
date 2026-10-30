#include "TestHarness.h"

#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "UI/Core/UIActorTreeBuilder.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"
#include "UI/Core/UISystem.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

struct RmlWarningCapture {
    std::vector<std::string> messages;
    RmlWarningCapture() {
        Logger::SetSink([this](const std::string& message) {
            if (message.find("[RmlUi]") != std::string::npos &&
                (message.find("[Warn]") != std::string::npos || message.find("[Error]") != std::string::npos))
                messages.push_back(message);
        });
    }
    ~RmlWarningCapture() { Logger::SetSink({}); }
};

bool CheckGridGeometry(Rml::ElementDocument* document, const std::vector<std::string>& ids, int columns,
                       float viewportWidth, float gap, float padding) {
    if (!Check(document != nullptr, "Generated grid document failed to load through RmlUi"))
        return false;
    const float cellWidth = (viewportWidth - 2.0f * padding - gap * (columns - 1)) / columns;
    for (size_t i = 0; i < ids.size(); ++i) {
        Rml::Element* child = document->GetElementById(ids[i]);
        if (!Check(child && child->IsVisible(), "Visible grid child was lost: " + ids[i]))
            return false;
        const Rml::Vector2f position = child->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = child->GetBox().GetSize(Rml::BoxArea::Border);
        const float expectedX = padding + static_cast<float>(i % columns) * (cellWidth + gap);
        const float expectedY = padding + static_cast<float>(i / columns) * (30.0f + gap);
        // Test the actor elements themselves, not just their generated wrappers. This catches absolute-positioned
        // children, non-sizing inline spans, and widget defaults overflowing an otherwise-correct grid cell.
        if (!Check(std::abs(position.x - expectedX) < 0.1f && std::abs(position.y - expectedY) < 0.1f &&
                       std::abs(size.x - cellWidth) < 0.1f && std::abs(size.y - 30.0f) < 0.1f,
                   "Wrong Rml grid child bounds for " + ids[i] + ": (" + std::to_string(position.x) + "," +
                       std::to_string(position.y) + ") " + std::to_string(size.x) + "x" + std::to_string(size.y)))
            return false;
    }
    return true;
}

bool TestUIGridLayoutRmlGeometry() {
    // UISystem owns the engine's existing shared Rml lifecycle. No window, graphics device, or GPU is required to
    // parse documents and execute the actual RmlUi flex formatter and measure final element boxes.
    UISystem ui;
    if (!Check(ui.Initialize(nullptr, nullptr), "Headless grid UISystem initialization failed"))
        return false;
    ui.Resize(320, 240);
    RmlWarningCapture warnings;
    Scene scene("Grid layout regression");
    Actor* canvasActor = scene.CreateActor("Canvas");
    auto* canvas = canvasActor->AddComponent<UICanvasComponent>();
    canvas->SetSourceMode(UICanvasSourceMode::ActorTree);
    Actor* gridActor = scene.CreateActor("Grid", canvasActor);
    auto* gridRect = gridActor->AddComponent<UIRectTransformComponent>();
    gridRect->GetRect().anchorMin = {0.0f, 0.0f};
    gridRect->GetRect().anchorMax = {1.0f, 0.0f};
    gridRect->GetRect().offsetMax = {0.0f, 220.0f};
    auto* grid = gridActor->AddComponent<UIGridLayoutComponent>();
    grid->SetElementID("grid");
    grid->columns = 3;
    grid->gap = 10.0f;
    grid->padding = 12.0f;
    std::vector<Actor*> children;
    const auto addChild = [&](const char* id, bool text, bool slider) {
        Actor* child = scene.CreateActor(id, gridActor);
        auto* rect = child->AddComponent<UIRectTransformComponent>();
        rect->GetRect().anchorMin = {0.5f, 0.5f};
        rect->GetRect().anchorMax = {0.5f, 0.5f};
        rect->GetRect().offsetMin = {13.0f, 9.0f};
        rect->GetRect().offsetMax = {1013.0f, 39.0f};
        if (text) {
            auto* label = child->AddComponent<UITextComponent>();
            label->SetElementID(id);
            label->text = "Grid text";
            label->fontSize = 16.0f;
        } else if (slider) {
            child->AddComponent<UISliderComponent>()->SetElementID(id);
        } else {
            auto* button = child->AddComponent<UIButtonComponent>();
            button->SetElementID(id);
            button->text.clear();
        }
        children.push_back(child);
        return child;
    };
    addChild("cell0", false, false);
    Actor* hidden = addChild("hidden", false, false);
    hidden->SetActive(false);
    addChild("cell1", true, false);
    addChild("cell2", false, true);
    addChild("cell3", false, false);
    addChild("cell4", false, false);
    const std::vector<std::string> visibleIDs = {"cell0", "cell1", "cell2", "cell3", "cell4"};

    ui.Update(scene, 0.016f);
    auto* document = canvas->GetCanvas().GetDocument();
    if (!CheckGridGeometry(document, visibleIDs, 3, 320.0f, 10.0f, 12.0f) ||
        !Check(document->GetElementById("hidden") == nullptr, "Inactive grid child consumed a cell"))
        return false;
    const std::string& generated = canvas->GetCanvas().GetMemoryDocumentSource();
    if (!Check(generated.find("display:grid") == std::string::npos &&
                   generated.find("grid-template-columns") == std::string::npos &&
                   generated.find("calc(") == std::string::npos,
               "Grid document still uses unsupported RmlUi CSS"))
        return false;

    // A partial final row must retain the same three column widths after viewport resizing.
    ui.Resize(500, 240);
    ui.Update(scene, 0.016f);
    if (!CheckGridGeometry(canvas->GetCanvas().GetDocument(), visibleIDs, 3, 500.0f, 10.0f, 12.0f))
        return false;

    // Rebuild after visibility and invalid-column edits; zero columns are safely treated as a single column.
    hidden->SetActive(true);
    grid->columns = 0;
    grid->gap = 0.0f;
    grid->padding = 4.0f;
    ui.Update(scene, 0.016f);
    if (!CheckGridGeometry(canvas->GetCanvas().GetDocument(), {"cell0", "hidden", "cell1", "cell2", "cell3", "cell4"},
                           1, 500.0f, 0.0f, 4.0f))
        return false;

    for (Actor* child : children)
        child->SetActive(false);
    ui.Update(scene, 0.016f);
    document = canvas->GetCanvas().GetDocument();
    if (!Check(document && document->GetElementById("grid") && document->GetElementById("cell0") == nullptr &&
                   canvas->GetCanvas().GetMemoryDocumentSource().find("ui-grid-row") == std::string::npos,
               "Empty grid retained visible cells or generated unnecessary rows"))
        return false;
    return Check(warnings.messages.empty(),
                 warnings.messages.empty()
                     ? ""
                     : "Generated grid emitted a real RmlUi parser/layout warning: " + warnings.messages.front());
}

MYENGINE_REGISTER_TEST("UI", "TestUIGridLayoutRmlGeometry", TestUIGridLayoutRmlGeometry);

} // namespace
