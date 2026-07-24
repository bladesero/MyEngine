#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Input/InputActionMap.h"
#include "Input/InputGlyphAtlas.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include <SDL3/SDL_gamepad.h>

enum class InputDeviceKind { KeyboardMouse, Gamepad };

// --------------------------------------------------------------------------
// Input - static snapshot updated once per frame by Engine.
//
// Key codes mirror SDL_Scancode values so callers can write:
//   if (Input::IsKeyDown(SDL_SCANCODE_W)) ...
// without coupling to SDL elsewhere.
// --------------------------------------------------------------------------
class MYENGINE_RUNTIME_API Input {
public:
    static constexpr int k_MaxKeys = 512;
    static constexpr int k_MaxButtons = 8; // mouse buttons
    static constexpr int k_MaxGamepads = 4;

    struct GamepadState {
        SDL_Gamepad* handle = nullptr;
        SDL_JoystickID instanceId = 0;
        bool connected = false;
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
        std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttonsPrev{};
        std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> axes{};
        std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> axesPrev{};
    };

    // ----- Keyboard ---------------------------------------------------------
    static bool IsKeyDown(int scancode);     // held this frame
    static bool IsKeyPressed(int scancode);  // went down this frame
    static bool IsKeyReleased(int scancode); // went up this frame

    // ----- Mouse ------------------------------------------------------------
    static bool IsMouseDown(int button); // button 1=left 2=mid 3=right
    static bool IsMousePressed(int button);
    static bool IsMouseReleased(int button);

    static int GetMouseX() { return s_MouseX; }
    static int GetMouseY() { return s_MouseY; }
    static int GetMouseRelX() { return s_MouseRelX; }
    static int GetMouseRelY() { return s_MouseRelY; }

    // ----- Gamepad ----------------------------------------------------------
    static int GetGamepadCount();
    static SDL_JoystickID GetPrimaryGamepadId();
    static bool IsGamepadConnected(SDL_JoystickID instanceId);
    static const char* GetGamepadName(SDL_JoystickID instanceId);

    static bool IsGamepadButtonDown(SDL_JoystickID instanceId, SDL_GamepadButton button);
    static bool IsGamepadButtonPressed(SDL_JoystickID instanceId, SDL_GamepadButton button);
    static bool IsGamepadButtonReleased(SDL_JoystickID instanceId, SDL_GamepadButton button);

    // Returns a normalized value in [-1, 1] for sticks and [0, 1] for triggers.
    static float GetGamepadAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis);
    static bool SetGamepadVibration(SDL_JoystickID instanceId, float lowFrequency, float highFrequency,
                                    uint32_t durationMs);
    static InputDeviceKind GetLastActiveDevice() { return s_LastActiveDevice; }
    static const char* GetGlyphSetName();
    static const char* GetGlyphFamilyName();
    static const char* GetGamepadGlyphFamily(SDL_GamepadType type);
    static bool LoadGlyphAtlasFromFile(const std::filesystem::path& path, std::string* error = nullptr);
    static bool LoadGlyphAtlasFromText(const std::string& text, std::string* error = nullptr);
    static void SetGlyphLocale(std::string locale);
    static const std::string& GetGlyphLocale() { return s_GlyphLocale; }
    static std::string GetActionGlyphJson(std::string_view actionName);
    static std::string GetSourceGlyphJson(std::string_view source);

    // ----- Project action map ----------------------------------------------
    static bool IsActionDown(std::string_view name);
    static bool IsActionPressed(std::string_view name);
    static bool IsActionReleased(std::string_view name);
    static float GetAxis1D(std::string_view name);
    static Math::Vec2 GetAxis2D(std::string_view name);
    static void SetGameplayInputEnabled(bool enabled);
    static bool IsGameplayInputEnabled();

    static bool LoadActionMapFromFile(const std::filesystem::path& path, std::string* error = nullptr);
    static void SetDefaultActionMap();
    static void ClearActionMap();
    static nlohmann::json GetActionMapJson();
    static bool ApplyActionMapOverrides(const nlohmann::json& value, std::string* error = nullptr);
    static void ResetActionMapOverrides();
    static std::vector<InputBindingConflict> FindBindingConflicts(std::string_view actionName, size_t bindingIndex,
                                                                  InputBindingPart part, std::string_view source);
    static bool RebindAction(std::string_view actionName, size_t bindingIndex, InputBindingPart part,
                             std::string_view source, bool allowConflicts = false, std::string* error = nullptr);
    static void SetRuntimePreferences(float mouseSensitivity, bool invertY, float gamepadDeadZone,
                                      float gamepadSensitivity, float vibrationStrength);
    static float GetMouseSensitivity() { return s_MouseSensitivity; }
    static bool GetInvertY() { return s_InvertY; }
    static float GetGamepadDeadZone() { return s_GamepadDeadZone; }
    static float GetGamepadSensitivity() { return s_GamepadSensitivity; }
    static float GetVibrationStrength() { return s_VibrationStrength; }

    // ----- Internal (called by Engine) --------------------------------------
    static void OnKeyDown(int scancode);
    static void OnKeyUp(int scancode);
    static void OnMouseButton(int button, bool down);
    static void OnMouseMove(int x, int y, int relX, int relY);
    static void OnGamepadAdded(SDL_JoystickID instanceId);
    static void OnGamepadRemoved(SDL_JoystickID instanceId);
    static void OnGamepadButton(SDL_JoystickID instanceId, SDL_GamepadButton button, bool down);
    static void OnGamepadAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis, Sint16 value);
    static void Shutdown();

    // Call at the START of each frame to advance prev->current.
    static void Flush();

private:
    static std::array<bool, k_MaxKeys> s_Keys;
    static std::array<bool, k_MaxKeys> s_KeysPrev;
    static std::array<bool, k_MaxButtons> s_Mouse;
    static std::array<bool, k_MaxButtons> s_MousePrev;
    static int s_MouseX;
    static int s_MouseY;
    static int s_MouseRelX;
    static int s_MouseRelY;
    static std::array<GamepadState, k_MaxGamepads> s_Gamepads;
    static InputActionMap s_ActionMap;
    static InputActionMap s_BaseActionMap;
    static InputDeviceKind s_LastActiveDevice;
    static float s_MouseSensitivity;
    static bool s_InvertY;
    static float s_GamepadDeadZone;
    static float s_GamepadSensitivity;
    static float s_VibrationStrength;
    static bool s_GameplayInputEnabled;
    static InputGlyphAtlas s_GlyphAtlas;
    static std::string s_GlyphLocale;

    static bool IsGamepadSubsystemReady();
    static int FindGamepadSlot(SDL_JoystickID instanceId);
    static int FindFreeGamepadSlot();
    static void CloseGamepadSlot(GamepadState& slot);
    static void SyncGamepadFromHandle(GamepadState& slot);
    static void RefreshGamepadInventory();
    static GamepadState* GetGamepadState(SDL_JoystickID instanceId);
    static const GamepadState* GetGamepadStateConst(SDL_JoystickID instanceId);
};
