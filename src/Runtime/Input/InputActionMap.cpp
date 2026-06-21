#include "Input/InputActionMap.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_map>

namespace {

void SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

std::string Lower(std::string_view value)
{
    std::string result(value);
    for (char& ch : result) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return result;
}

std::string TrimPrefix(std::string_view text, std::string_view prefix)
{
    return std::string(text.substr(prefix.size()));
}

bool ParseType(std::string_view text, InputActionType& type)
{
    const std::string lower = Lower(text);
    if (lower == "button") {
        type = InputActionType::Button;
        return true;
    }
    if (lower == "axis1d") {
        type = InputActionType::Axis1D;
        return true;
    }
    if (lower == "axis2d") {
        type = InputActionType::Axis2D;
        return true;
    }
    return false;
}

std::string TypeName(InputActionType type)
{
    switch (type) {
    case InputActionType::Button: return "Button";
    case InputActionType::Axis1D: return "Axis1D";
    case InputActionType::Axis2D: return "Axis2D";
    }
    return "Button";
}

bool ParseMouse(std::string_view name, InputSource& source)
{
    static const std::unordered_map<std::string, int> buttons = {
        {"left", SDL_BUTTON_LEFT},
        {"middle", SDL_BUTTON_MIDDLE},
        {"right", SDL_BUTTON_RIGHT},
        {"x1", SDL_BUTTON_X1},
        {"x2", SDL_BUTTON_X2},
    };
    const std::string lower = Lower(name);
    if (lower == "deltax") {
        source.kind = InputSourceKind::MouseDeltaX;
        source.name = "Mouse/DeltaX";
        return true;
    }
    if (lower == "deltay") {
        source.kind = InputSourceKind::MouseDeltaY;
        source.name = "Mouse/DeltaY";
        return true;
    }
    if (auto it = buttons.find(lower); it != buttons.end()) {
        source.kind = InputSourceKind::MouseButton;
        source.code = it->second;
        source.name = "Mouse/" + std::string(name);
        return true;
    }
    return false;
}

bool ParseGamepad(std::string_view name, InputSource& source)
{
    static const std::unordered_map<std::string, SDL_GamepadButton> buttons = {
        {"south", SDL_GAMEPAD_BUTTON_SOUTH},
        {"east", SDL_GAMEPAD_BUTTON_EAST},
        {"west", SDL_GAMEPAD_BUTTON_WEST},
        {"north", SDL_GAMEPAD_BUTTON_NORTH},
        {"back", SDL_GAMEPAD_BUTTON_BACK},
        {"guide", SDL_GAMEPAD_BUTTON_GUIDE},
        {"start", SDL_GAMEPAD_BUTTON_START},
        {"leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK},
        {"rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
        {"leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
        {"rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"dpup", SDL_GAMEPAD_BUTTON_DPAD_UP},
        {"dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {"dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
        {"dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    };
    static const std::unordered_map<std::string, SDL_GamepadAxis> axes = {
        {"leftx", SDL_GAMEPAD_AXIS_LEFTX},
        {"lefty", SDL_GAMEPAD_AXIS_LEFTY},
        {"rightx", SDL_GAMEPAD_AXIS_RIGHTX},
        {"righty", SDL_GAMEPAD_AXIS_RIGHTY},
        {"lefttrigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
        {"righttrigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
    };
    const std::string lower = Lower(name);
    if (auto it = buttons.find(lower); it != buttons.end()) {
        source.kind = InputSourceKind::GamepadButton;
        source.code = static_cast<int>(it->second);
        source.name = "Gamepad/" + std::string(name);
        return true;
    }
    if (auto it = axes.find(lower); it != axes.end()) {
        source.kind = InputSourceKind::GamepadAxis;
        source.code = static_cast<int>(it->second);
        source.name = "Gamepad/" + std::string(name);
        return true;
    }
    return false;
}

bool ParseKeyboard(std::string_view name, InputSource& source)
{
    static const std::unordered_map<std::string, SDL_Scancode> aliases = {
        {"space", SDL_SCANCODE_SPACE},
        {"escape", SDL_SCANCODE_ESCAPE},
        {"esc", SDL_SCANCODE_ESCAPE},
        {"enter", SDL_SCANCODE_RETURN},
        {"return", SDL_SCANCODE_RETURN},
        {"tab", SDL_SCANCODE_TAB},
        {"leftshift", SDL_SCANCODE_LSHIFT},
        {"rightshift", SDL_SCANCODE_RSHIFT},
        {"leftctrl", SDL_SCANCODE_LCTRL},
        {"rightctrl", SDL_SCANCODE_RCTRL},
        {"leftalt", SDL_SCANCODE_LALT},
        {"rightalt", SDL_SCANCODE_RALT},
    };
    const std::string lower = Lower(name);
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    if (auto it = aliases.find(lower); it != aliases.end()) {
        scancode = it->second;
    } else {
        scancode = SDL_GetScancodeFromName(std::string(name).c_str());
    }
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    source.kind = InputSourceKind::KeyboardKey;
    source.code = static_cast<int>(scancode);
    source.name = "Keyboard/" + std::string(name);
    return true;
}

nlohmann::json SourceToJson(const InputSource& source)
{
    return source.name;
}

nlohmann::json BindingToJson(const InputBinding& binding, InputActionType type)
{
    nlohmann::json json = nlohmann::json::object();
    if (type == InputActionType::Axis2D) {
        if (binding.x.IsValid()) json["x"] = SourceToJson(binding.x);
        if (binding.y.IsValid()) json["y"] = SourceToJson(binding.y);
        if (binding.scaleX != 1.0f) json["scaleX"] = binding.scaleX;
        if (binding.scaleY != 1.0f) json["scaleY"] = binding.scaleY;
    } else {
        json["source"] = SourceToJson(binding.source);
        if (binding.scale != 1.0f) json["scale"] = binding.scale;
    }
    if (binding.deadZone > 0.0f) json["deadZone"] = binding.deadZone;
    return json;
}

} // namespace

bool InputActionMap::ParseSource(std::string_view text, InputSource& source, std::string* error)
{
    source = {};
    const std::string value(text);
    const std::string lower = Lower(value);
    if (lower.rfind("keyboard/", 0) == 0) {
        if (ParseKeyboard(TrimPrefix(value, "Keyboard/"), source)) return true;
    } else if (lower.rfind("mouse/", 0) == 0) {
        if (ParseMouse(TrimPrefix(value, "Mouse/"), source)) return true;
    } else if (lower.rfind("gamepad/", 0) == 0) {
        if (ParseGamepad(TrimPrefix(value, "Gamepad/"), source)) return true;
    } else {
        SetError(error, "input source must start with Keyboard/, Mouse/, or Gamepad/: " + value);
        return false;
    }
    SetError(error, "unknown input source: " + value);
    return false;
}

bool InputActionMap::LoadFromFile(const std::filesystem::path& path, std::string* error)
{
    if (error) error->clear();
    std::ifstream input(path);
    if (!input) {
        SetError(error, "failed to open input config: " + path.string());
        return false;
    }
    try {
        nlohmann::json json;
        input >> json;
        return LoadFromJson(json, error);
    } catch (const std::exception& exception) {
        SetError(error, "failed to parse input config: " + std::string(exception.what()));
        return false;
    }
}

bool InputActionMap::LoadFromJson(const nlohmann::json& json, std::string* error)
{
    if (error) error->clear();
    if (!json.is_object()) {
        SetError(error, "input config root must be an object");
        return false;
    }
    if (json.value("version", 0) != kCurrentVersion) {
        SetError(error, "unsupported input config version");
        return false;
    }
    const auto actionsIt = json.find("actions");
    if (actionsIt == json.end() || !actionsIt->is_array()) {
        SetError(error, "input config actions must be an array");
        return false;
    }

    std::vector<InputAction> loaded;
    for (const auto& actionJson : *actionsIt) {
        if (!actionJson.is_object()) {
            SetError(error, "input action must be an object");
            return false;
        }
        InputAction action;
        action.name = actionJson.value("name", std::string{});
        if (action.name.empty()) {
            SetError(error, "input action name must not be empty");
            return false;
        }
        const std::string typeName = actionJson.value("type", std::string{});
        if (!ParseType(typeName, action.type)) {
            SetError(error, "unknown input action type: " + typeName);
            return false;
        }
        const auto bindingsIt = actionJson.find("bindings");
        if (bindingsIt == actionJson.end() || !bindingsIt->is_array() || bindingsIt->empty()) {
            SetError(error, "input action bindings must be a non-empty array: " + action.name);
            return false;
        }
        for (const auto& bindingJson : *bindingsIt) {
            if (!bindingJson.is_object()) {
                SetError(error, "input binding must be an object: " + action.name);
                return false;
            }
            InputBinding binding;
            binding.deadZone = std::clamp(bindingJson.value("deadZone", 0.0f), 0.0f, 1.0f);
            if (action.type == InputActionType::Axis2D) {
                binding.scaleX = bindingJson.value("scaleX", 1.0f);
                binding.scaleY = bindingJson.value("scaleY", 1.0f);
                if (const auto x = bindingJson.find("x"); x != bindingJson.end()) {
                    if (!x->is_string() || !ParseSource(x->get<std::string>(), binding.x, error)) return false;
                }
                if (const auto y = bindingJson.find("y"); y != bindingJson.end()) {
                    if (!y->is_string() || !ParseSource(y->get<std::string>(), binding.y, error)) return false;
                }
                if (!binding.x.IsValid() && !binding.y.IsValid()) {
                    SetError(error, "Axis2D binding must define x or y: " + action.name);
                    return false;
                }
            } else {
                const auto sourceIt = bindingJson.find("source");
                if (sourceIt == bindingJson.end() || !sourceIt->is_string()) {
                    SetError(error, "input binding source must be a string: " + action.name);
                    return false;
                }
                if (!ParseSource(sourceIt->get<std::string>(), binding.source, error)) return false;
                binding.scale = bindingJson.value("scale", 1.0f);
            }
            action.bindings.push_back(std::move(binding));
        }
        loaded.push_back(std::move(action));
    }
    m_Actions = std::move(loaded);
    return true;
}

bool InputActionMap::SaveToFile(const std::filesystem::path& path, std::string* error) const
{
    if (error) error->clear();
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            SetError(error, "failed to create input config directory: " + ec.message());
            return false;
        }
    }

    nlohmann::json actions = nlohmann::json::array();
    for (const InputAction& action : m_Actions) {
        nlohmann::json bindings = nlohmann::json::array();
        for (const InputBinding& binding : action.bindings) {
            bindings.push_back(BindingToJson(binding, action.type));
        }
        actions.push_back({
            {"name", action.name},
            {"type", TypeName(action.type)},
            {"bindings", std::move(bindings)},
        });
    }
    const nlohmann::json root = {
        {"version", kCurrentVersion},
        {"actions", std::move(actions)},
    };
    std::ofstream output(path);
    if (!output) {
        SetError(error, "failed to write input config: " + path.string());
        return false;
    }
    output << root.dump(2) << '\n';
    return true;
}

void InputActionMap::Clear()
{
    m_Actions.clear();
}

const InputAction* InputActionMap::FindAction(std::string_view name) const
{
    for (const InputAction& action : m_Actions) {
        if (action.name == name) {
            return &action;
        }
    }
    return nullptr;
}

InputActionMap InputActionMap::CreateDefault()
{
    const nlohmann::json json = {
        {"version", kCurrentVersion},
        {"actions", nlohmann::json::array({
            {
                {"name", "Jump"},
                {"type", "Button"},
                {"bindings", nlohmann::json::array({
                    {{"source", "Keyboard/Space"}},
                    {{"source", "Gamepad/South"}},
                })},
            },
            {
                {"name", "Move"},
                {"type", "Axis2D"},
                {"bindings", nlohmann::json::array({
                    {{"x", "Keyboard/D"}, {"scaleX", 1.0f}},
                    {{"x", "Keyboard/A"}, {"scaleX", -1.0f}},
                    {{"y", "Keyboard/W"}, {"scaleY", 1.0f}},
                    {{"y", "Keyboard/S"}, {"scaleY", -1.0f}},
                    {{"x", "Gamepad/LeftX"}, {"y", "Gamepad/LeftY"}, {"deadZone", 0.15f}, {"scaleY", -1.0f}},
                })},
            },
            {
                {"name", "Look"},
                {"type", "Axis2D"},
                {"bindings", nlohmann::json::array({
                    {{"x", "Mouse/DeltaX"}, {"y", "Mouse/DeltaY"}, {"scaleX", 1.0f}, {"scaleY", 1.0f}},
                    {{"x", "Gamepad/RightX"}, {"y", "Gamepad/RightY"}, {"deadZone", 0.15f}, {"scaleY", -1.0f}},
                })},
            },
            {
                {"name", "Throttle"},
                {"type", "Axis1D"},
                {"bindings", nlohmann::json::array({
                    {{"source", "Gamepad/RightTrigger"}},
                    {{"source", "Gamepad/LeftTrigger"}, {"scale", -1.0f}},
                })},
            },
        })},
    };
    InputActionMap map;
    std::string ignored;
    map.LoadFromJson(json, &ignored);
    return map;
}

bool InputActionMap::WriteDefaultFile(const std::filesystem::path& path, std::string* error)
{
    return CreateDefault().SaveToFile(path, error);
}
