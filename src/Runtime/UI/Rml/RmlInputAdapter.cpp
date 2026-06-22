#include "UI/Rml/RmlInputAdapter.h"

#include "Input/Input.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Input.h>
#include <SDL3/SDL_scancode.h>

int RmlInputAdapter::GetKeyModifierState() const
{
    int modifiers = 0;
    if (Input::IsKeyDown(SDL_SCANCODE_LCTRL) || Input::IsKeyDown(SDL_SCANCODE_RCTRL))
        modifiers |= Rml::Input::KM_CTRL;
    if (Input::IsKeyDown(SDL_SCANCODE_LSHIFT) || Input::IsKeyDown(SDL_SCANCODE_RSHIFT))
        modifiers |= Rml::Input::KM_SHIFT;
    if (Input::IsKeyDown(SDL_SCANCODE_LALT) || Input::IsKeyDown(SDL_SCANCODE_RALT))
        modifiers |= Rml::Input::KM_ALT;
    if (Input::IsKeyDown(SDL_SCANCODE_LGUI) || Input::IsKeyDown(SDL_SCANCODE_RGUI))
        modifiers |= Rml::Input::KM_META;
    return modifiers;
}

int RmlInputAdapter::ToRmlMouseButton(int button) const
{
    if (button <= 0) return 0;
    return button - 1;
}

int RmlInputAdapter::ToRmlKey(int scancode) const
{
    using namespace Rml::Input;
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        return KI_A + (scancode - SDL_SCANCODE_A);
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
        return KI_1 + (scancode - SDL_SCANCODE_1);
    if (scancode == SDL_SCANCODE_0) return KI_0;
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
        return KI_F1 + (scancode - SDL_SCANCODE_F1);

    switch (scancode) {
    case SDL_SCANCODE_SPACE: return KI_SPACE;
    case SDL_SCANCODE_BACKSPACE: return KI_BACK;
    case SDL_SCANCODE_TAB: return KI_TAB;
    case SDL_SCANCODE_RETURN: return KI_RETURN;
    case SDL_SCANCODE_ESCAPE: return KI_ESCAPE;
    case SDL_SCANCODE_PAGEUP: return KI_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return KI_NEXT;
    case SDL_SCANCODE_END: return KI_END;
    case SDL_SCANCODE_HOME: return KI_HOME;
    case SDL_SCANCODE_LEFT: return KI_LEFT;
    case SDL_SCANCODE_UP: return KI_UP;
    case SDL_SCANCODE_RIGHT: return KI_RIGHT;
    case SDL_SCANCODE_DOWN: return KI_DOWN;
    case SDL_SCANCODE_INSERT: return KI_INSERT;
    case SDL_SCANCODE_DELETE: return KI_DELETE;
    case SDL_SCANCODE_LSHIFT: return KI_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return KI_RSHIFT;
    case SDL_SCANCODE_LCTRL: return KI_LCONTROL;
    case SDL_SCANCODE_RCTRL: return KI_RCONTROL;
    case SDL_SCANCODE_LALT: return KI_LMENU;
    case SDL_SCANCODE_RALT: return KI_RMENU;
    default: return KI_UNKNOWN;
    }
}

bool RmlInputAdapter::ProcessEvent(Rml::Context& context, const Event& event) const
{
    const int modifiers = GetKeyModifierState();
    switch (event.type) {
    case EventType::KeyDown:
        return context.ProcessKeyDown(
            static_cast<Rml::Input::KeyIdentifier>(ToRmlKey(event.key.scancode)), modifiers);
    case EventType::KeyUp:
        return context.ProcessKeyUp(
            static_cast<Rml::Input::KeyIdentifier>(ToRmlKey(event.key.scancode)), modifiers);
    case EventType::TextInput:
        return context.ProcessTextInput(event.textInput.text);
    case EventType::MouseMove:
        return context.ProcessMouseMove(event.mouseMove.x, event.mouseMove.y, modifiers);
    case EventType::MouseButtonDown:
        return context.ProcessMouseButtonDown(
            ToRmlMouseButton(event.mouseButton.button), modifiers);
    case EventType::MouseButtonUp:
        return context.ProcessMouseButtonUp(
            ToRmlMouseButton(event.mouseButton.button), modifiers);
    case EventType::MouseWheel:
        return context.ProcessMouseWheel(
            Rml::Vector2f(event.mouseWheel.x, event.mouseWheel.y), modifiers);
    default:
        return false;
    }
}
