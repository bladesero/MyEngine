#pragma once

#include <array>
#include <cstdint>

#include <SDL3/SDL_gamepad.h>

// --------------------------------------------------------------------------
// Input - static snapshot updated once per frame by Engine.
//
// Key codes mirror SDL_Scancode values so callers can write:
//   if (Input::IsKeyDown(SDL_SCANCODE_W)) ...
// without coupling to SDL elsewhere.
// --------------------------------------------------------------------------
class Input {
public:
    static constexpr int k_MaxKeys    = 512;
    static constexpr int k_MaxButtons = 8;   // mouse buttons
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
    static bool IsKeyDown(int scancode);          // held this frame
    static bool IsKeyPressed(int scancode);       // went down this frame
    static bool IsKeyReleased(int scancode);      // went up this frame

    // ----- Mouse ------------------------------------------------------------
    static bool IsMouseDown(int button);          // button 1=left 2=mid 3=right
    static bool IsMousePressed(int button);
    static bool IsMouseReleased(int button);

    static int GetMouseX()    { return s_MouseX; }
    static int GetMouseY()    { return s_MouseY; }
    static int GetMouseRelX() { return s_MouseRelX; }
    static int GetMouseRelY() { return s_MouseRelY; }

    // ----- Gamepad ----------------------------------------------------------
    static int            GetGamepadCount();
    static SDL_JoystickID GetPrimaryGamepadId();
    static bool           IsGamepadConnected(SDL_JoystickID instanceId);
    static const char*    GetGamepadName(SDL_JoystickID instanceId);

    static bool IsGamepadButtonDown(SDL_JoystickID instanceId, SDL_GamepadButton button);
    static bool IsGamepadButtonPressed(SDL_JoystickID instanceId, SDL_GamepadButton button);
    static bool IsGamepadButtonReleased(SDL_JoystickID instanceId, SDL_GamepadButton button);

    // Returns a normalized value in [-1, 1] for sticks and [0, 1] for triggers.
    static float GetGamepadAxis(SDL_JoystickID instanceId, SDL_GamepadAxis axis);

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
    static std::array<bool, k_MaxKeys>    s_Keys;
    static std::array<bool, k_MaxKeys>    s_KeysPrev;
    static std::array<bool, k_MaxButtons> s_Mouse;
    static std::array<bool, k_MaxButtons> s_MousePrev;
    static int s_MouseX, s_MouseY, s_MouseRelX, s_MouseRelY;
    static std::array<GamepadState, k_MaxGamepads> s_Gamepads;

    static bool IsGamepadSubsystemReady();
    static int FindGamepadSlot(SDL_JoystickID instanceId);
    static int FindFreeGamepadSlot();
    static void CloseGamepadSlot(GamepadState& slot);
    static void SyncGamepadFromHandle(GamepadState& slot);
    static void RefreshGamepadInventory();
    static GamepadState* GetGamepadState(SDL_JoystickID instanceId);
    static const GamepadState* GetGamepadStateConst(SDL_JoystickID instanceId);
};
