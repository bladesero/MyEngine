#include "Input.h"

#include "../Core/Logger.h"

#include <SDL3/SDL.h>

#include <algorithm>

// Static storage
std::array<bool, Input::k_MaxKeys>    Input::s_Keys     = {};
std::array<bool, Input::k_MaxKeys>    Input::s_KeysPrev = {};
std::array<bool, Input::k_MaxButtons> Input::s_Mouse     = {};
std::array<bool, Input::k_MaxButtons> Input::s_MousePrev = {};
int Input::s_MouseX    = 0;
int Input::s_MouseY    = 0;
int Input::s_MouseRelX = 0;
int Input::s_MouseRelY = 0;
std::array<Input::GamepadState, Input::k_MaxGamepads> Input::s_Gamepads = {};

bool Input::IsGamepadSubsystemReady() {
    return (SDL_WasInit(SDL_INIT_GAMEPAD) != 0);
}

int Input::FindGamepadSlot(SDL_JoystickID instanceId) {
    for (int i = 0; i < k_MaxGamepads; ++i) {
        if (s_Gamepads[i].connected && s_Gamepads[i].instanceId == instanceId) {
            return i;
        }
    }
    return -1;
}

int Input::FindFreeGamepadSlot() {
    for (int i = 0; i < k_MaxGamepads; ++i) {
        if (!s_Gamepads[i].connected) {
            return i;
        }
    }
    return -1;
}

void Input::CloseGamepadSlot(GamepadState& slot) {
    if (slot.handle) {
        SDL_CloseGamepad(slot.handle);
        slot.handle = nullptr;
    }
    slot.connected = false;
    slot.instanceId = 0;
    slot.buttons.fill(false);
    slot.buttonsPrev.fill(false);
    slot.axes.fill(0);
    slot.axesPrev.fill(0);
}

void Input::SyncGamepadFromHandle(GamepadState& slot) {
    if (!slot.handle) {
        return;
    }

    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
        slot.buttons[button] = SDL_GetGamepadButton(slot.handle,
            static_cast<SDL_GamepadButton>(button));
    }

    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
        slot.axes[axis] = SDL_GetGamepadAxis(slot.handle,
            static_cast<SDL_GamepadAxis>(axis));
    }
}

void Input::RefreshGamepadInventory() {
    if (!IsGamepadSubsystemReady()) {
        return;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        const SDL_JoystickID instanceId = ids[i];
        const int slotIndex = FindGamepadSlot(instanceId);
        if (slotIndex >= 0) {
            auto& slot = s_Gamepads[slotIndex];
            if (!slot.handle) {
                slot.handle = SDL_OpenGamepad(instanceId);
                if (!slot.handle) {
                    Logger::Warn("Failed to open gamepad ", instanceId, ": ", SDL_GetError());
                }
            }
            continue;
        }

        const int freeSlot = FindFreeGamepadSlot();
        if (freeSlot < 0) {
            Logger::Warn("Too many gamepads connected; ignoring instance ", instanceId);
            continue;
        }

        auto& slot = s_Gamepads[freeSlot];
        slot.connected = true;
        slot.instanceId = instanceId;
        slot.handle = SDL_OpenGamepad(instanceId);
        if (!slot.handle) {
            Logger::Warn("Failed to open gamepad ", instanceId, ": ", SDL_GetError());
            slot.connected = false;
            slot.instanceId = 0;
            continue;
        }
        SyncGamepadFromHandle(slot);
        slot.buttonsPrev = slot.buttons;
        slot.axesPrev    = slot.axes;
    }

    for (int i = 0; i < k_MaxGamepads; ++i) {
        auto& slot = s_Gamepads[i];
        if (!slot.connected) {
            continue;
        }

        bool stillPresent = false;
        for (int j = 0; j < count; ++j) {
            if (ids[j] == slot.instanceId) {
                stillPresent = true;
                break;
            }
        }

        if (!stillPresent || (slot.handle && !SDL_GamepadConnected(slot.handle))) {
            CloseGamepadSlot(slot);
            continue;
        }

        if (slot.handle) {
            SyncGamepadFromHandle(slot);
        }
    }

    SDL_free(ids);
}

Input::GamepadState* Input::GetGamepadState(SDL_JoystickID instanceId) {
    const int slotIndex = FindGamepadSlot(instanceId);
    if (slotIndex >= 0) {
        return &s_Gamepads[slotIndex];
    }

    const int freeSlot = FindFreeGamepadSlot();
    if (freeSlot < 0) {
        return nullptr;
    }

    auto& slot = s_Gamepads[freeSlot];
    slot.connected = true;
    slot.instanceId = instanceId;
    return &slot;
}

const Input::GamepadState* Input::GetGamepadStateConst(SDL_JoystickID instanceId) {
    const int slotIndex = FindGamepadSlot(instanceId);
    if (slotIndex >= 0) {
        return &s_Gamepads[slotIndex];
    }

    return nullptr;
}

void Input::Flush() {
    s_KeysPrev   = s_Keys;
    s_MousePrev  = s_Mouse;
    s_MouseRelX  = 0;
    s_MouseRelY  = 0;

    for (auto& slot : s_Gamepads) {
        slot.buttonsPrev = slot.buttons;
        slot.axesPrev    = slot.axes;
    }

    RefreshGamepadInventory();
}

void Input::Shutdown() {
    for (auto& slot : s_Gamepads) {
        CloseGamepadSlot(slot);
    }
    s_Keys.fill(false);
    s_KeysPrev.fill(false);
    s_Mouse.fill(false);
    s_MousePrev.fill(false);
    s_MouseX = 0;
    s_MouseY = 0;
    s_MouseRelX = 0;
    s_MouseRelY = 0;
}

// ---- Keyboard ---------------------------------------------------------------
void Input::OnKeyDown(int sc) {
    if (sc >= 0 && sc < k_MaxKeys) s_Keys[sc] = true;
}
void Input::OnKeyUp(int sc) {
    if (sc >= 0 && sc < k_MaxKeys) s_Keys[sc] = false;
}

bool Input::IsKeyDown(int sc) {
    return (sc >= 0 && sc < k_MaxKeys) && s_Keys[sc];
}
bool Input::IsKeyPressed(int sc) {
    return (sc >= 0 && sc < k_MaxKeys) && s_Keys[sc] && !s_KeysPrev[sc];
}
bool Input::IsKeyReleased(int sc) {
    return (sc >= 0 && sc < k_MaxKeys) && !s_Keys[sc] && s_KeysPrev[sc];
}

// ---- Mouse ------------------------------------------------------------------
void Input::OnMouseButton(int btn, bool down) {
    if (btn >= 1 && btn < k_MaxButtons) s_Mouse[btn] = down;
}
void Input::OnMouseMove(int x, int y, int relX, int relY) {
    s_MouseX = x;  s_MouseY = y;
    s_MouseRelX = relX; s_MouseRelY = relY;
}

bool Input::IsMouseDown(int btn) {
    return (btn >= 1 && btn < k_MaxButtons) && s_Mouse[btn];
}
bool Input::IsMousePressed(int btn) {
    return (btn >= 1 && btn < k_MaxButtons) && s_Mouse[btn] && !s_MousePrev[btn];
}
bool Input::IsMouseReleased(int btn) {
    return (btn >= 1 && btn < k_MaxButtons) && !s_Mouse[btn] && s_MousePrev[btn];
}

// ---- Gamepad ----------------------------------------------------------------
int Input::GetGamepadCount() {
    int count = 0;
    for (const auto& slot : s_Gamepads) {
        if (slot.connected) {
            ++count;
        }
    }
    return count;
}

SDL_JoystickID Input::GetPrimaryGamepadId() {
    for (const auto& slot : s_Gamepads) {
        if (slot.connected) {
            return slot.instanceId;
        }
    }
    return 0;
}

bool Input::IsGamepadConnected(SDL_JoystickID instanceId) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    return slot != nullptr && slot->connected;
}

const char* Input::GetGamepadName(SDL_JoystickID instanceId) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    if (!slot || !slot->handle) {
        return nullptr;
    }
    return SDL_GetGamepadName(slot->handle);
}

bool Input::IsGamepadButtonDown(SDL_JoystickID instanceId, SDL_GamepadButton button) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int buttonIndex = static_cast<int>(button);
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT
        && slot->buttons[buttonIndex];
}

bool Input::IsGamepadButtonPressed(SDL_JoystickID instanceId, SDL_GamepadButton button) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int buttonIndex = static_cast<int>(button);
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT
        && slot->buttons[buttonIndex] && !slot->buttonsPrev[buttonIndex];
}

bool Input::IsGamepadButtonReleased(SDL_JoystickID instanceId, SDL_GamepadButton button) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int buttonIndex = static_cast<int>(button);
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT
        && !slot->buttons[buttonIndex] && slot->buttonsPrev[buttonIndex];
}

float Input::GetGamepadAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int axisIndex = static_cast<int>(axis);
    if (!slot || axisIndex < 0 || axisIndex >= SDL_GAMEPAD_AXIS_COUNT) {
        return 0.0f;
    }

    const float raw = static_cast<float>(slot->axes[axisIndex]);
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        return std::clamp(raw / 32767.0f, 0.0f, 1.0f);
    }

    return std::clamp(raw / 32767.0f, -1.0f, 1.0f);
}

void Input::OnGamepadAdded(SDL_JoystickID instanceId) {
    GamepadState* slot = GetGamepadState(instanceId);
    if (!slot) {
        Logger::Warn("Ignoring gamepad add for instance ", instanceId, " because no slots are free");
        return;
    }

    slot->connected = true;
    slot->instanceId = instanceId;

    if (IsGamepadSubsystemReady()) {
        if (!slot->handle) {
            slot->handle = SDL_OpenGamepad(instanceId);
            if (!slot->handle) {
                Logger::Warn("Failed to open gamepad ", instanceId, ": ", SDL_GetError());
            }
        }
        SyncGamepadFromHandle(*slot);
    }

    slot->buttonsPrev = slot->buttons;
    slot->axesPrev    = slot->axes;
}

void Input::OnGamepadRemoved(SDL_JoystickID instanceId) {
    const int slotIndex = FindGamepadSlot(instanceId);
    if (slotIndex < 0) {
        return;
    }

    CloseGamepadSlot(s_Gamepads[slotIndex]);
}

void Input::OnGamepadButton(SDL_JoystickID instanceId, SDL_GamepadButton button, bool down) {
    GamepadState* slot = GetGamepadState(instanceId);
    if (!slot) {
        return;
    }

    const int buttonIndex = static_cast<int>(button);
    if (buttonIndex < 0 || buttonIndex >= SDL_GAMEPAD_BUTTON_COUNT) {
        return;
    }

    slot->connected = true;
    slot->instanceId = instanceId;
    slot->buttons[buttonIndex] = down;
}

void Input::OnGamepadAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis, Sint16 value) {
    GamepadState* slot = GetGamepadState(instanceId);
    if (!slot) {
        return;
    }

    const int axisIndex = static_cast<int>(axis);
    if (axisIndex < 0 || axisIndex >= SDL_GAMEPAD_AXIS_COUNT) {
        return;
    }

    slot->connected = true;
    slot->instanceId = instanceId;
    slot->axes[axisIndex] = value;
}
