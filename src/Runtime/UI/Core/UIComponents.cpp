#include "UI/Core/UIComponents.h"

namespace {

nlohmann::json Vec2ToJson(const Vec2& value)
{
    return nlohmann::json::array({value.x, value.y});
}

Vec2 Vec2FromJson(const nlohmann::json& data, const Vec2& fallback)
{
    if (!data.is_array() || data.size() < 2) return fallback;
    return {data[0].get<float>(), data[1].get<float>()};
}

nlohmann::json ColorToJson(const Color& value)
{
    return nlohmann::json::array({value.r, value.g, value.b, value.a});
}

Color ColorFromJson(const nlohmann::json& data, const Color& fallback)
{
    if (!data.is_array() || data.size() < 4) return fallback;
    return {data[0].get<float>(), data[1].get<float>(),
            data[2].get<float>(), data[3].get<float>()};
}

} // namespace

void UIElementComponent::Serialize(nlohmann::json& data) const
{
    Component::Serialize(data);
    data["elementId"] = m_ElementID;
    data["className"] = m_ClassName;
}

void UIElementComponent::Deserialize(const nlohmann::json& data)
{
    Component::Deserialize(data);
    m_ElementID = data.value("elementId", std::string{});
    m_ClassName = data.value("className", std::string{});
}

void UIRectTransformComponent::Serialize(nlohmann::json& data) const
{
    Component::Serialize(data);
    data["anchorMin"] = Vec2ToJson(rect.anchorMin);
    data["anchorMax"] = Vec2ToJson(rect.anchorMax);
    data["offsetMin"] = Vec2ToJson(rect.offsetMin);
    data["offsetMax"] = Vec2ToJson(rect.offsetMax);
    data["pivot"] = Vec2ToJson(rect.pivot);
}

void UIRectTransformComponent::Deserialize(const nlohmann::json& data)
{
    Component::Deserialize(data);
    rect.anchorMin = Vec2FromJson(data.value("anchorMin", nlohmann::json{}), rect.anchorMin);
    rect.anchorMax = Vec2FromJson(data.value("anchorMax", nlohmann::json{}), rect.anchorMax);
    rect.offsetMin = Vec2FromJson(data.value("offsetMin", nlohmann::json{}), rect.offsetMin);
    rect.offsetMax = Vec2FromJson(data.value("offsetMax", nlohmann::json{}), rect.offsetMax);
    rect.pivot = Vec2FromJson(data.value("pivot", nlohmann::json{}), rect.pivot);
}

void UITextComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["text"] = text;
    data["fontSize"] = fontSize;
    data["color"] = ColorToJson(color);
}

void UITextComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    text = data.value("text", text);
    fontSize = data.value("fontSize", fontSize);
    color = ColorFromJson(data.value("color", nlohmann::json{}), color);
}

void UIImageComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["source"] = source;
    data["tint"] = ColorToJson(tint);
}

void UIImageComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    source = data.value("source", source);
    tint = ColorFromJson(data.value("tint", nlohmann::json{}), tint);
}

void UIButtonComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["text"] = text;
    data["disabled"] = disabled;
}

void UIButtonComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    text = data.value("text", text);
    disabled = data.value("disabled", disabled);
}

void UISliderComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["value"] = value;
    data["min"] = min;
    data["max"] = max;
    data["step"] = step;
    data["dataBinding"] = dataBinding;
}

void UISliderComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    value = data.value("value", value);
    min = data.value("min", min);
    max = data.value("max", max);
    step = data.value("step", step);
    dataBinding = data.value("dataBinding", dataBinding);
}

void UIProgressBarComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["value"] = value;
    data["max"] = max;
}

void UIProgressBarComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    value = data.value("value", value);
    max = data.value("max", max);
}

void UIScrollViewComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["horizontal"] = horizontal;
    data["vertical"] = vertical;
}

void UIScrollViewComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    horizontal = data.value("horizontal", horizontal);
    vertical = data.value("vertical", vertical);
}

void UILayoutGroupComponent::Serialize(nlohmann::json& data) const
{
    UIElementComponent::Serialize(data);
    data["spacing"] = spacing;
    data["padding"] = padding;
    data["alignItems"] = alignItems;
    data["justifyContent"] = justifyContent;
}

void UILayoutGroupComponent::Deserialize(const nlohmann::json& data)
{
    UIElementComponent::Deserialize(data);
    spacing = data.value("spacing", spacing);
    padding = data.value("padding", padding);
    alignItems = data.value("alignItems", alignItems);
    justifyContent = data.value("justifyContent", justifyContent);
}

void UIGridLayoutComponent::Serialize(nlohmann::json& data) const
{
    UILayoutGroupComponent::Serialize(data);
    data["columns"] = columns;
    data["rows"] = rows;
    data["gap"] = gap;
}

void UIGridLayoutComponent::Deserialize(const nlohmann::json& data)
{
    UILayoutGroupComponent::Deserialize(data);
    columns = data.value("columns", columns);
    rows = data.value("rows", rows);
    gap = data.value("gap", gap);
}
