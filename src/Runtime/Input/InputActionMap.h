#pragma once

#include "Math/Vector2.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

enum class InputActionType {
    Button,
    Axis1D,
    Axis2D
};

enum class InputSourceKind {
    None,
    KeyboardKey,
    MouseButton,
    MouseDeltaX,
    MouseDeltaY,
    GamepadButton,
    GamepadAxis
};

struct InputSource {
    InputSourceKind kind = InputSourceKind::None;
    int code = 0;
    std::string name;

    bool IsValid() const { return kind != InputSourceKind::None; }
};

struct InputBinding {
    InputSource source;
    InputSource x;
    InputSource y;
    float scale = 1.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float deadZone = 0.0f;
};

struct InputAction {
    std::string name;
    InputActionType type = InputActionType::Button;
    std::vector<InputBinding> bindings;
};

class InputActionMap {
public:
    static constexpr int kCurrentVersion = 1;
    static constexpr const char* kDefaultProjectPath = "Content/Config/Input.input.json";

    bool LoadFromFile(const std::filesystem::path& path, std::string* error = nullptr);
    bool LoadFromJson(const nlohmann::json& json, std::string* error = nullptr);
    bool SaveToFile(const std::filesystem::path& path, std::string* error = nullptr) const;

    void Clear();
    const InputAction* FindAction(std::string_view name) const;
    const std::vector<InputAction>& GetActions() const { return m_Actions; }

    static InputActionMap CreateDefault();
    static bool WriteDefaultFile(const std::filesystem::path& path, std::string* error = nullptr);
    static bool ParseSource(std::string_view text, InputSource& source, std::string* error = nullptr);

private:
    std::vector<InputAction> m_Actions;
};
