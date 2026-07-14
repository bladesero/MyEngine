#include "UI/Input/UIInputSystem.h"

#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"
#include "UI/Core/UISystem.h"
#include "UI/UIEventBridge.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

std::string ElementIDFor(const Actor& actor, const UIElementComponent& element) {
    if (!element.GetElementID().empty())
        return element.GetElementID();
    return "ui_actor_" + std::to_string(actor.GetID());
}

bool HitElement(Rml::ElementDocument* document, const std::string& elementID, int x, int y) {
    if (!document || elementID.empty())
        return false;
    Rml::Element* element = document->GetElementById(elementID);
    return element && element->IsVisible(true) &&
           element->IsPointWithinElement(Rml::Vector2f(static_cast<float>(x), static_cast<float>(y)));
}

bool GetRectTransformBounds(const Actor& actor, float& left, float& top, float& width, float& height) {
    const auto* rect = actor.GetComponent<UIRectTransformComponent>();
    if (!rect)
        return false;

    const RectTransform& r = rect->GetRect();
    if (r.anchorMin.x != r.anchorMax.x || r.anchorMin.y != r.anchorMax.y) {
        return false;
    }

    left = r.offsetMin.x;
    top = r.offsetMin.y;
    width = r.offsetMax.x - r.offsetMin.x;
    height = r.offsetMax.y - r.offsetMin.y;
    return width > 0.0f && height > 0.0f;
}

bool HitWidget(Rml::ElementDocument* document, const std::string& elementID, const Actor& actor, int x, int y) {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    if (GetRectTransformBounds(actor, left, top, width, height)) {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        return fx >= left && fx <= left + width && fy >= top && fy <= top + height;
    }

    return HitElement(document, elementID, x, y);
}

float SliderValueFromMouse(const Actor& actor, Rml::Element* element, float mouseX, float minValue, float maxValue,
                           float step) {
    if (maxValue <= minValue)
        return minValue;
    float left = 0.0f;
    float width = 0.0f;
    float fallbackTop = 0.0f;
    float fallbackHeight = 0.0f;
    if (!GetRectTransformBounds(actor, left, fallbackTop, width, fallbackHeight) && element) {
        left = element->GetAbsoluteLeft();
        width = element->GetClientWidth();
    }
    if (width <= 0.0f)
        return minValue;
    width = std::max(1.0f, width);
    const float ratio = std::clamp((mouseX - left) / width, 0.0f, 1.0f);
    float value = minValue + ratio * (maxValue - minValue);
    if (step > 0.0f) {
        value = minValue + std::round((value - minValue) / step) * step;
    }
    return std::clamp(value, minValue, maxValue);
}

bool IsMouseEvent(EventType type) {
    return type == EventType::MouseMove || type == EventType::MouseButtonDown || type == EventType::MouseButtonUp ||
           type == EventType::MouseWheel;
}

} // namespace

bool UIInputSystem::ConvertMouseEvent(Event& event, const UIInputViewport& viewport, Event& outLocalEvent, int& outX,
                                      int& outY) const {
    outLocalEvent = event;
    int windowX = 0;
    int windowY = 0;
    switch (event.type) {
    case EventType::MouseMove:
        windowX = event.mouseMove.x;
        windowY = event.mouseMove.y;
        break;
    case EventType::MouseButtonDown:
    case EventType::MouseButtonUp:
        windowX = event.mouseButton.x;
        windowY = event.mouseButton.y;
        break;
    case EventType::MouseWheel:
        windowX = 0;
        windowY = 0;
        break;
    default:
        return false;
    }

    if (event.type != EventType::MouseWheel) {
        const bool inside = windowX >= viewport.x && windowY >= viewport.y && windowX < viewport.x + viewport.width &&
                            windowY < viewport.y + viewport.height;
        if (!inside && !m_Capture.active)
            return false;

        const float sx = viewport.scaleX != 0.0f ? viewport.scaleX : 1.0f;
        const float sy = viewport.scaleY != 0.0f ? viewport.scaleY : 1.0f;
        outX = static_cast<int>((static_cast<float>(windowX - viewport.x)) * sx);
        outY = static_cast<int>((static_cast<float>(windowY - viewport.y)) * sy);
        if (event.type == EventType::MouseMove) {
            outLocalEvent.mouseMove.x = outX;
            outLocalEvent.mouseMove.y = outY;
        } else {
            outLocalEvent.mouseButton.x = outX;
            outLocalEvent.mouseButton.y = outY;
        }
    }
    return true;
}

bool UIInputSystem::ProcessCapturedSlider(Scene& scene, Event& event, int localX, UIEventBridge& eventBridge,
                                          UIDataModel* (*resolveDataModel)(void*, const std::string&),
                                          void* dataModelUser) {
    if (!m_Capture.active)
        return false;
    Actor* canvasActor = scene.FindByID(m_Capture.canvasActorID);
    Actor* actor = scene.FindByID(m_Capture.actorID);
    if (!canvasActor || !actor) {
        m_Capture = {};
        return false;
    }
    auto* canvas = canvasActor->GetComponent<UICanvasComponent>();
    auto* slider = actor->GetComponent<UISliderComponent>();
    if (!canvas || !slider) {
        m_Capture = {};
        return false;
    }

    if (event.type == EventType::MouseMove || event.type == EventType::MouseButtonDown ||
        event.type == EventType::MouseButtonUp) {
        Rml::ElementDocument* document = canvas->GetCanvas().GetDocument();
        Rml::Element* element = document ? document->GetElementById(m_Capture.elementID) : nullptr;
        const float nextValue =
            SliderValueFromMouse(*actor, element, static_cast<float>(localX), slider->min, slider->max, slider->step);
        if (std::abs(nextValue - slider->value) > 0.0001f) {
            slider->value = nextValue;
            if (!slider->dataBinding.empty() && resolveDataModel) {
                if (UIDataModel* model = resolveDataModel(dataModelUser, slider->dataBinding)) {
                    model->SetFloat(slider->dataBinding, nextValue);
                }
            }
            eventBridge.Emit(&canvas->GetCanvas(), {m_Capture.elementID, "change", nextValue, true});
        }
        if (event.type == EventType::MouseButtonUp) {
            m_Capture = {};
        }
        return true;
    }
    return false;
}

bool UIInputSystem::ProcessMouseEvent(Scene& scene, Event& event, int localX, int localY, UIEventBridge& eventBridge,
                                      UIDataModel* (*resolveDataModel)(void*, const std::string&),
                                      void* dataModelUser) {
    if (ProcessCapturedSlider(scene, event, localX, eventBridge, resolveDataModel, dataModelUser)) {
        return true;
    }

    bool consumed = false;
    scene.ForEach([&](Actor& canvasActor) {
        if (consumed || !canvasActor.IsActive())
            return;
        auto* canvasComponent = canvasActor.GetComponent<UICanvasComponent>();
        if (!canvasComponent || !canvasComponent->IsEnabled() || !canvasComponent->IsVisible() ||
            !canvasComponent->IsInteractive() || canvasComponent->GetInputMode() == UIInputMode::None) {
            return;
        }
        UICanvas& canvas = canvasComponent->GetCanvas();
        Rml::ElementDocument* document = canvas.GetDocument();
        if (!document)
            return;

        std::function<void(Actor&)> visit = [&](Actor& actor) {
            if (consumed || !actor.IsActiveSelf())
                return;
            if (auto* slider = actor.GetComponent<UISliderComponent>()) {
                const std::string elementID = ElementIDFor(actor, *slider);
                if (HitWidget(document, elementID, actor, localX, localY)) {
                    if (event.type == EventType::MouseButtonDown && event.mouseButton.button == 1) {
                        m_Capture = {true, canvasActor.GetID(), actor.GetID(), elementID};
                        ProcessCapturedSlider(scene, event, localX, eventBridge, resolveDataModel, dataModelUser);
                    }
                    consumed = true;
                    return;
                }
            }
            if (auto* button = actor.GetComponent<UIButtonComponent>()) {
                const std::string elementID = ElementIDFor(actor, *button);
                if (HitWidget(document, elementID, actor, localX, localY)) {
                    if (!button->disabled && event.type == EventType::MouseButtonDown &&
                        event.mouseButton.button == 1) {
                        m_PressedButtonCanvasID = canvasActor.GetID();
                        m_PressedButtonActorID = actor.GetID();
                        m_PressedButtonElementID = elementID;
                    } else if (!button->disabled && event.type == EventType::MouseButtonUp &&
                               event.mouseButton.button == 1 && m_PressedButtonCanvasID == canvasActor.GetID() &&
                               m_PressedButtonActorID == actor.GetID()) {
                        eventBridge.Emit(&canvas, {elementID, "click", 0.0f, false});
                        m_PressedButtonCanvasID = 0;
                        m_PressedButtonActorID = 0;
                        m_PressedButtonElementID.clear();
                    }
                    consumed = true;
                    return;
                }
            }
            for (Actor* child : actor.GetChildren()) {
                if (child)
                    visit(*child);
            }
        };

        for (Actor* child : canvasActor.GetChildren()) {
            if (child)
                visit(*child);
            if (consumed)
                break;
        }
    });

    if (event.type == EventType::MouseButtonUp && event.mouseButton.button == 1 && !consumed) {
        m_PressedButtonCanvasID = 0;
        m_PressedButtonActorID = 0;
        m_PressedButtonElementID.clear();
    }
    return consumed;
}

bool UIInputSystem::ProcessEvent(Scene& scene, Rml::Context& context, Event& event, const UIInputViewport& viewport,
                                 UIEventBridge& eventBridge,
                                 UIDataModel* (*resolveDataModel)(void*, const std::string&), void* dataModelUser) {
    (void)context;
    if (!viewport.enabled || event.handled)
        return false;
    if (IsMouseEvent(event.type) && !viewport.hovered && !m_Capture.active)
        return false;

    Event localEvent;
    int localX = 0;
    int localY = 0;
    if (IsMouseEvent(event.type)) {
        if (!ConvertMouseEvent(event, viewport, localEvent, localX, localY))
            return false;
        return ProcessMouseEvent(scene, localEvent, localX, localY, eventBridge, resolveDataModel, dataModelUser);
    }
    return false;
}
