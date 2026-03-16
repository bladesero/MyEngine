#pragma once

#include <array>

// --------------------------------------------------------------------------
// Input  –  static snapshot updated once per frame by Engine.
//
// Key codes mirror SDL_Scancode values so callers can write:
//   if (Input::IsKeyDown(SDL_SCANCODE_W)) ...
// without coupling to SDL elsewhere.
// --------------------------------------------------------------------------
class Input {
public:
    static constexpr int k_MaxKeys    = 512;
    static constexpr int k_MaxButtons = 8;   // mouse buttons

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

    // ----- Internal (called by Engine) --------------------------------------
    static void OnKeyDown(int scancode);
    static void OnKeyUp(int scancode);
    static void OnMouseButton(int button, bool down);
    static void OnMouseMove(int x, int y, int relX, int relY);
    // Call at the START of each frame to advance prev→current.
    static void Flush();

private:
    static std::array<bool, k_MaxKeys>    s_Keys;
    static std::array<bool, k_MaxKeys>    s_KeysPrev;
    static std::array<bool, k_MaxButtons> s_Mouse;
    static std::array<bool, k_MaxButtons> s_MousePrev;
    static int s_MouseX, s_MouseY, s_MouseRelX, s_MouseRelY;
};
