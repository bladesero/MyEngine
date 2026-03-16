#include "Input.h"

// Static storage
std::array<bool, Input::k_MaxKeys>    Input::s_Keys     = {};
std::array<bool, Input::k_MaxKeys>    Input::s_KeysPrev = {};
std::array<bool, Input::k_MaxButtons> Input::s_Mouse     = {};
std::array<bool, Input::k_MaxButtons> Input::s_MousePrev = {};
int Input::s_MouseX    = 0;
int Input::s_MouseY    = 0;
int Input::s_MouseRelX = 0;
int Input::s_MouseRelY = 0;

void Input::Flush() {
    s_KeysPrev   = s_Keys;
    s_MousePrev  = s_Mouse;
    s_MouseRelX  = 0;
    s_MouseRelY  = 0;
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
