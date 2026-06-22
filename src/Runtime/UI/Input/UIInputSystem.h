#pragma once

#include "Core/Event.h"

#include <cstdint>
#include <string>

namespace Rml {
class Context;
}

class Scene;
class UIDataModel;
class UIEventBridge;

struct UIInputViewport {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool enabled = true;
    bool hovered = true;
};

class UIInputSystem {
public:
    bool ProcessEvent(Scene& scene, Rml::Context& context, Event& event,
                      const UIInputViewport& viewport, UIEventBridge& eventBridge,
                      UIDataModel* (*resolveDataModel)(void*, const std::string&),
                      void* dataModelUser);

private:
    struct PointerCapture {
        bool active = false;
        uint64_t canvasActorID = 0;
        uint64_t actorID = 0;
        std::string elementID;
    };

    bool ConvertMouseEvent(Event& event, const UIInputViewport& viewport,
                           Event& outLocalEvent, int& outX, int& outY) const;
    bool ProcessMouseEvent(Scene& scene, Event& event, int localX, int localY,
                           UIEventBridge& eventBridge,
                           UIDataModel* (*resolveDataModel)(void*, const std::string&),
                           void* dataModelUser);
    bool ProcessCapturedSlider(Scene& scene, Event& event, int localX,
                               UIEventBridge& eventBridge,
                               UIDataModel* (*resolveDataModel)(void*, const std::string&),
                               void* dataModelUser);

    PointerCapture m_Capture;
    uint64_t m_PressedButtonCanvasID = 0;
    uint64_t m_PressedButtonActorID = 0;
    std::string m_PressedButtonElementID;
};
