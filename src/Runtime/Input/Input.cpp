#include "Input.h"

#include "../Core/Logger.h"
#include "../Core/RuntimeFileSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace {

float ApplyDeadZone(float value, float deadZone) {
    return std::fabs(value) <= deadZone ? 0.0f : value;
}

float ClampAxis(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

float Dominant(float current, float candidate) {
    return std::fabs(candidate) > std::fabs(current) ? candidate : current;
}

} // namespace

// Static storage
std::array<bool, Input::k_MaxKeys> Input::s_Keys = {};
std::array<bool, Input::k_MaxKeys> Input::s_KeysPrev = {};
std::array<bool, Input::k_MaxButtons> Input::s_Mouse = {};
std::array<bool, Input::k_MaxButtons> Input::s_MousePrev = {};
int Input::s_MouseX = 0;
int Input::s_MouseY = 0;
int Input::s_MouseRelX = 0;
int Input::s_MouseRelY = 0;
std::array<Input::GamepadState, Input::k_MaxGamepads> Input::s_Gamepads = {};
InputActionMap Input::s_ActionMap = InputActionMap::CreateDefault();
InputActionMap Input::s_BaseActionMap = InputActionMap::CreateDefault();
InputDeviceKind Input::s_LastActiveDevice = InputDeviceKind::KeyboardMouse;
float Input::s_MouseSensitivity = 1.0f;
bool Input::s_InvertY = false;
float Input::s_GamepadDeadZone = 0.15f;
float Input::s_GamepadSensitivity = 1.0f;
float Input::s_VibrationStrength = 1.0f;
bool Input::s_GameplayInputEnabled = true;
InputGlyphAtlas Input::s_GlyphAtlas;
std::string Input::s_GlyphLocale = "en";

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
        slot.buttons[button] = SDL_GetGamepadButton(slot.handle, static_cast<SDL_GamepadButton>(button));
    }

    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis) {
        slot.axes[axis] = SDL_GetGamepadAxis(slot.handle, static_cast<SDL_GamepadAxis>(axis));
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
        slot.axesPrev = slot.axes;
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
    s_KeysPrev = s_Keys;
    s_MousePrev = s_Mouse;
    s_MouseRelX = 0;
    s_MouseRelY = 0;

    for (auto& slot : s_Gamepads) {
        slot.buttonsPrev = slot.buttons;
        slot.axesPrev = slot.axes;
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
    s_ActionMap.Clear();
    s_BaseActionMap.Clear();
    SetRuntimePreferences(1.0f, false, 0.15f, 1.0f, 1.0f);
    s_LastActiveDevice = InputDeviceKind::KeyboardMouse;
    s_GameplayInputEnabled = true;
}

// ---- Keyboard ---------------------------------------------------------------
void Input::OnKeyDown(int sc) {
    if (sc >= 0 && sc < k_MaxKeys) {
        s_Keys[sc] = true;
        s_LastActiveDevice = InputDeviceKind::KeyboardMouse;
    }
}
void Input::OnKeyUp(int sc) {
    if (sc >= 0 && sc < k_MaxKeys)
        s_Keys[sc] = false;
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
    if (btn >= 1 && btn < k_MaxButtons) {
        s_Mouse[btn] = down;
        if (down)
            s_LastActiveDevice = InputDeviceKind::KeyboardMouse;
    }
}
void Input::OnMouseMove(int x, int y, int relX, int relY) {
    s_MouseX = x;
    s_MouseY = y;
    s_MouseRelX = relX;
    s_MouseRelY = relY;
    if (relX != 0 || relY != 0)
        s_LastActiveDevice = InputDeviceKind::KeyboardMouse;
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
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT && slot->buttons[buttonIndex];
}

bool Input::IsGamepadButtonPressed(SDL_JoystickID instanceId, SDL_GamepadButton button) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int buttonIndex = static_cast<int>(button);
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT && slot->buttons[buttonIndex] &&
           !slot->buttonsPrev[buttonIndex];
}

bool Input::IsGamepadButtonReleased(SDL_JoystickID instanceId, SDL_GamepadButton button) {
    const GamepadState* slot = GetGamepadStateConst(instanceId);
    const int buttonIndex = static_cast<int>(button);
    return slot && buttonIndex >= 0 && buttonIndex < SDL_GAMEPAD_BUTTON_COUNT && !slot->buttons[buttonIndex] &&
           slot->buttonsPrev[buttonIndex];
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

bool Input::SetGamepadVibration(SDL_JoystickID instanceId, float lowFrequency, float highFrequency,
                                uint32_t durationMs) {
    GamepadState* slot = GetGamepadState(instanceId);
    if (!slot || !slot->handle)
        return false;
    const Uint16 low = static_cast<Uint16>(std::clamp(lowFrequency * s_VibrationStrength, 0.0f, 1.0f) * 65535.0f);
    const Uint16 high = static_cast<Uint16>(std::clamp(highFrequency * s_VibrationStrength, 0.0f, 1.0f) * 65535.0f);
    return SDL_RumbleGamepad(slot->handle, low, high, durationMs);
}

const char* Input::GetGlyphSetName() {
    return s_LastActiveDevice == InputDeviceKind::Gamepad ? "gamepad" : "keyboardMouse";
}

const char* Input::GetGamepadGlyphFamily(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return "playstation";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return "nintendo";
    default:
        return "xbox";
    }
}

const char* Input::GetGlyphFamilyName() {
    if (s_LastActiveDevice != InputDeviceKind::Gamepad)
        return "keyboardMouse";
    const GamepadState* slot = GetGamepadStateConst(GetPrimaryGamepadId());
    return GetGamepadGlyphFamily(slot && slot->handle ? SDL_GetGamepadType(slot->handle) : SDL_GAMEPAD_TYPE_STANDARD);
}

bool Input::LoadGlyphAtlasFromFile(const std::filesystem::path& path, std::string* error) {
    std::string text;
    if (!RuntimeFileSystem::Get().ReadText(path.string(), text, error))
        return false;
    return LoadGlyphAtlasFromText(text, error);
}

bool Input::LoadGlyphAtlasFromText(const std::string& text, std::string* error) {
    InputGlyphAtlas parsed;
    if (!InputGlyphAtlas::FromText(text, parsed, error))
        return false;
    s_GlyphAtlas = std::move(parsed);
    if (s_GlyphLocale.empty())
        s_GlyphLocale = s_GlyphAtlas.GetDefaultLocale();
    return true;
}

void Input::SetGlyphLocale(std::string locale) {
    if (!locale.empty())
        s_GlyphLocale = std::move(locale);
}

std::string Input::GetActionGlyphJson(std::string_view actionName) {
    const InputAction* action = s_ActionMap.FindAction(actionName);
    if (!action)
        return nlohmann::json{{"valid", false}, {"action", actionName}}.dump();
    const bool wantGamepad = s_LastActiveDevice == InputDeviceKind::Gamepad;
    const InputSource* selected = nullptr;
    auto consider = [&](const InputSource& source) {
        if (selected || !source.IsValid())
            return;
        const bool gamepad =
            source.kind == InputSourceKind::GamepadButton || source.kind == InputSourceKind::GamepadAxis;
        if (gamepad == wantGamepad)
            selected = &source;
    };
    for (const InputBinding& binding : action->bindings) {
        consider(binding.source);
        consider(binding.x);
        consider(binding.y);
    }
    if (!selected) {
        for (const InputBinding& binding : action->bindings) {
            if (binding.source.IsValid()) {
                selected = &binding.source;
                break;
            }
            if (binding.x.IsValid()) {
                selected = &binding.x;
                break;
            }
            if (binding.y.IsValid()) {
                selected = &binding.y;
                break;
            }
        }
    }
    if (!selected)
        return nlohmann::json{{"valid", false}, {"action", actionName}}.dump();
    nlohmann::json result = s_GlyphAtlas.Resolve(GetGlyphFamilyName(), selected->name, s_GlyphLocale).ToJson();
    result["action"] = actionName;
    result["locale"] = s_GlyphLocale;
    return result.dump();
}

std::string Input::GetSourceGlyphJson(std::string_view source) {
    nlohmann::json result = s_GlyphAtlas.Resolve(GetGlyphFamilyName(), std::string(source), s_GlyphLocale).ToJson();
    result["locale"] = s_GlyphLocale;
    return result.dump();
}

// ---- Project action map -----------------------------------------------------
namespace {

float ReadSourceValue(const InputSource& source) {
    switch (source.kind) {
    case InputSourceKind::KeyboardKey:
        return Input::IsKeyDown(source.code) ? 1.0f : 0.0f;
    case InputSourceKind::MouseButton:
        return Input::IsMouseDown(source.code) ? 1.0f : 0.0f;
    case InputSourceKind::MouseDeltaX:
        return ClampAxis(static_cast<float>(Input::GetMouseRelX()));
    case InputSourceKind::MouseDeltaY:
        return ClampAxis(static_cast<float>(Input::GetMouseRelY()));
    case InputSourceKind::GamepadButton: {
        const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
        return pad != 0 && Input::IsGamepadButtonDown(pad, static_cast<SDL_GamepadButton>(source.code)) ? 1.0f : 0.0f;
    }
    case InputSourceKind::GamepadAxis: {
        const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
        return pad != 0 ? Input::GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(source.code)) : 0.0f;
    }
    case InputSourceKind::None:
        break;
    }
    return 0.0f;
}

float EffectiveScale(const InputSource& source, bool yAxis) {
    float scale = 1.0f;
    if (source.kind == InputSourceKind::MouseDeltaX || source.kind == InputSourceKind::MouseDeltaY)
        scale *= Input::GetMouseSensitivity();
    if (source.kind == InputSourceKind::GamepadAxis)
        scale *= Input::GetGamepadSensitivity();
    if (yAxis && Input::GetInvertY() &&
        (source.kind == InputSourceKind::MouseDeltaY || source.kind == InputSourceKind::GamepadAxis))
        scale *= -1.0f;
    return scale;
}

float EffectiveDeadZone(const InputSource& source, float bindingDeadZone) {
    return source.kind == InputSourceKind::GamepadAxis ? std::max(bindingDeadZone, Input::GetGamepadDeadZone())
                                                       : bindingDeadZone;
}

bool ReadSourceDown(const InputSource& source) {
    switch (source.kind) {
    case InputSourceKind::KeyboardKey:
        return Input::IsKeyDown(source.code);
    case InputSourceKind::MouseButton:
        return Input::IsMouseDown(source.code);
    case InputSourceKind::MouseDeltaX:
    case InputSourceKind::MouseDeltaY:
    case InputSourceKind::GamepadAxis:
        return std::fabs(ReadSourceValue(source)) > 0.0f;
    case InputSourceKind::GamepadButton: {
        const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
        return pad != 0 && Input::IsGamepadButtonDown(pad, static_cast<SDL_GamepadButton>(source.code));
    }
    case InputSourceKind::None:
        break;
    }
    return false;
}

bool ReadSourcePressed(const InputSource& source) {
    switch (source.kind) {
    case InputSourceKind::KeyboardKey:
        return Input::IsKeyPressed(source.code);
    case InputSourceKind::MouseButton:
        return Input::IsMousePressed(source.code);
    case InputSourceKind::GamepadButton: {
        const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
        return pad != 0 && Input::IsGamepadButtonPressed(pad, static_cast<SDL_GamepadButton>(source.code));
    }
    default:
        break;
    }
    return false;
}

bool ReadSourceReleased(const InputSource& source) {
    switch (source.kind) {
    case InputSourceKind::KeyboardKey:
        return Input::IsKeyReleased(source.code);
    case InputSourceKind::MouseButton:
        return Input::IsMouseReleased(source.code);
    case InputSourceKind::GamepadButton: {
        const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
        return pad != 0 && Input::IsGamepadButtonReleased(pad, static_cast<SDL_GamepadButton>(source.code));
    }
    default:
        break;
    }
    return false;
}

} // namespace

bool Input::IsActionDown(std::string_view name) {
    if (!s_GameplayInputEnabled)
        return false;
    const InputAction* action = s_ActionMap.FindAction(name);
    if (!action || action->type != InputActionType::Button)
        return false;
    for (const InputBinding& binding : action->bindings) {
        if (ReadSourceDown(binding.source))
            return true;
    }
    return false;
}

void Input::SetGameplayInputEnabled(bool enabled) {
    s_GameplayInputEnabled = enabled;
}
bool Input::IsGameplayInputEnabled() {
    return s_GameplayInputEnabled;
}

bool Input::IsActionPressed(std::string_view name) {
    if (!s_GameplayInputEnabled)
        return false;
    const InputAction* action = s_ActionMap.FindAction(name);
    if (!action || action->type != InputActionType::Button)
        return false;
    for (const InputBinding& binding : action->bindings) {
        if (ReadSourcePressed(binding.source))
            return true;
    }
    return false;
}

bool Input::IsActionReleased(std::string_view name) {
    if (!s_GameplayInputEnabled)
        return false;
    const InputAction* action = s_ActionMap.FindAction(name);
    if (!action || action->type != InputActionType::Button)
        return false;
    for (const InputBinding& binding : action->bindings) {
        if (ReadSourceReleased(binding.source))
            return true;
    }
    return false;
}

float Input::GetAxis1D(std::string_view name) {
    if (!s_GameplayInputEnabled)
        return 0.0f;
    const InputAction* action = s_ActionMap.FindAction(name);
    if (!action || action->type != InputActionType::Axis1D)
        return 0.0f;
    float value = 0.0f;
    for (const InputBinding& binding : action->bindings) {
        const float candidate =
            ApplyDeadZone(ReadSourceValue(binding.source) * binding.scale * EffectiveScale(binding.source, false),
                          EffectiveDeadZone(binding.source, binding.deadZone));
        value = Dominant(value, candidate);
    }
    return ClampAxis(value);
}

Math::Vec2 Input::GetAxis2D(std::string_view name) {
    if (!s_GameplayInputEnabled)
        return {};
    const InputAction* action = s_ActionMap.FindAction(name);
    if (!action || action->type != InputActionType::Axis2D)
        return {};
    Math::Vec2 value;
    for (const InputBinding& binding : action->bindings) {
        if (binding.x.IsValid()) {
            value.x = Dominant(
                value.x, ApplyDeadZone(ReadSourceValue(binding.x) * binding.scaleX * EffectiveScale(binding.x, false),
                                       EffectiveDeadZone(binding.x, binding.deadZone)));
        }
        if (binding.y.IsValid()) {
            value.y = Dominant(
                value.y, ApplyDeadZone(ReadSourceValue(binding.y) * binding.scaleY * EffectiveScale(binding.y, true),
                                       EffectiveDeadZone(binding.y, binding.deadZone)));
        }
    }
    return {ClampAxis(value.x), ClampAxis(value.y)};
}

bool Input::LoadActionMapFromFile(const std::filesystem::path& path, std::string* error) {
    InputActionMap loaded;
    if (!loaded.LoadFromFile(path, error)) {
        s_ActionMap = InputActionMap::CreateDefault();
        return false;
    }
    s_ActionMap = std::move(loaded);
    s_BaseActionMap = s_ActionMap;
    return true;
}

void Input::SetDefaultActionMap() {
    s_ActionMap = InputActionMap::CreateDefault();
    s_BaseActionMap = s_ActionMap;
}

void Input::ClearActionMap() {
    s_ActionMap.Clear();
    s_BaseActionMap.Clear();
}

nlohmann::json Input::GetActionMapJson() {
    return s_ActionMap.ToJson();
}

bool Input::ApplyActionMapOverrides(const nlohmann::json& value, std::string* error) {
    if (value.is_null()) {
        ResetActionMapOverrides();
        return true;
    }
    InputActionMap loaded;
    if (!loaded.LoadFromJson(value, error))
        return false;
    s_ActionMap = std::move(loaded);
    return true;
}

void Input::ResetActionMapOverrides() {
    s_ActionMap = s_BaseActionMap;
}

std::vector<InputBindingConflict> Input::FindBindingConflicts(std::string_view actionName, size_t bindingIndex,
                                                              InputBindingPart part, std::string_view source) {
    return s_ActionMap.FindConflicts(actionName, bindingIndex, part, source);
}

bool Input::RebindAction(std::string_view actionName, size_t bindingIndex, InputBindingPart part,
                         std::string_view source, bool allowConflicts, std::string* error) {
    return s_ActionMap.Rebind(actionName, bindingIndex, part, source, allowConflicts, error);
}

void Input::SetRuntimePreferences(float mouseSensitivity, bool invertY, float gamepadDeadZone, float gamepadSensitivity,
                                  float vibrationStrength) {
    s_MouseSensitivity = std::clamp(mouseSensitivity, 0.01f, 10.0f);
    s_InvertY = invertY;
    s_GamepadDeadZone = std::clamp(gamepadDeadZone, 0.0f, 0.95f);
    s_GamepadSensitivity = std::clamp(gamepadSensitivity, 0.1f, 5.0f);
    s_VibrationStrength = std::clamp(vibrationStrength, 0.0f, 1.0f);
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
    slot->axesPrev = slot->axes;
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
    if (down)
        s_LastActiveDevice = InputDeviceKind::Gamepad;
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
    if (std::abs(static_cast<int>(value)) > 4096)
        s_LastActiveDevice = InputDeviceKind::Gamepad;
}
