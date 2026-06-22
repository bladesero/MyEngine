#include "UI/Core/UICanvasComponent.h"

#include "Assets/AssetManager.h"

namespace {

std::vector<std::string> ReadStringArray(const nlohmann::json& data, const char* key)
{
    std::vector<std::string> result;
    if (!data.contains(key) || !data[key].is_array()) return result;
    for (const auto& entry : data[key]) {
        if (entry.is_string()) result.push_back(entry.get<std::string>());
    }
    return result;
}

const char* InputModeToString(UIInputMode mode)
{
    switch (mode) {
    case UIInputMode::None: return "None";
    case UIInputMode::UIOnly: return "UIOnly";
    case UIInputMode::GameAndUI: return "GameAndUI";
    }
    return "GameAndUI";
}

UIInputMode ParseInputMode(const std::string& value)
{
    if (value == "None") return UIInputMode::None;
    if (value == "UIOnly") return UIInputMode::UIOnly;
    return UIInputMode::GameAndUI;
}

} // namespace

UICanvasComponent::UICanvasComponent()
    : m_Canvas(std::make_unique<UICanvas>())
{}

UICanvasComponent::~UICanvasComponent() = default;

bool UICanvasComponent::LoadDocument(const std::string& path)
{
    return m_Canvas->LoadDocument(path);
}

bool UICanvasComponent::Reload()
{
    return m_Canvas->Reload();
}

void UICanvasComponent::Serialize(nlohmann::json& data) const
{
    Component::Serialize(data);
    data["documentPath"] = AssetManager::Get().MakeProjectRelativePath(GetDocumentPath());
    data["stylePaths"] = GetStylePaths();
    data["defaultFontPaths"] = GetDefaultFontPaths();
    data["canvasSpace"] = "Screen";
    data["sortOrder"] = GetSortOrder();
    data["inputMode"] = InputModeToString(GetInputMode());
    data["visible"] = IsVisible();
    data["interactive"] = IsInteractive();
}

void UICanvasComponent::Deserialize(const nlohmann::json& data)
{
    Component::Deserialize(data);
    SetDocumentPath(data.value("documentPath", std::string{}));
    SetStylePaths(ReadStringArray(data, "stylePaths"));
    SetDefaultFontPaths(ReadStringArray(data, "defaultFontPaths"));
    SetSortOrder(data.value("sortOrder", 0));
    SetInputMode(ParseInputMode(data.value("inputMode", std::string{"GameAndUI"})));
    SetVisible(data.value("visible", true));
    SetInteractive(data.value("interactive", true));
}
