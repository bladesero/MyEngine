#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"
#include "UI/Core/RectTransform.h"

#include <string>

class UIElementComponent : public Component {
public:
    const std::string& GetElementID() const { return m_ElementID; }
    void SetElementID(std::string id) { m_ElementID = std::move(id); }
    const std::string& GetClassName() const { return m_ClassName; }
    void SetClassName(std::string className) { m_ClassName = std::move(className); }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

protected:
    std::string m_ElementID;
    std::string m_ClassName;
};

class UIRectTransformComponent final : public Component {
public:
    const char* GetTypeName() const override { return "UIRectTransform"; }

    RectTransform& GetRect() { return rect; }
    const RectTransform& GetRect() const { return rect; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    RectTransform rect;
};

class UITextComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UIText"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    std::string text = "Text";
    float fontSize = 24.0f;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

class UIImageComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UIImage"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    std::string source;
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

class UIButtonComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UIButton"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    std::string text = "Button";
    bool disabled = false;
};

class UISliderComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UISlider"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    std::string dataBinding;
};

class UIProgressBarComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UIProgressBar"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    float value = 0.0f;
    float max = 1.0f;
};

class UIScrollViewComponent final : public UIElementComponent {
public:
    const char* GetTypeName() const override { return "UIScrollView"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    bool horizontal = false;
    bool vertical = true;
};

class UILayoutGroupComponent : public UIElementComponent {
public:
    float spacing = 8.0f;
    float padding = 0.0f;
    std::string alignItems = "stretch";
    std::string justifyContent = "flex-start";

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;
};

class UIVerticalLayoutComponent final : public UILayoutGroupComponent {
public:
    const char* GetTypeName() const override { return "UIVerticalLayout"; }
};

class UIHorizontalLayoutComponent final : public UILayoutGroupComponent {
public:
    const char* GetTypeName() const override { return "UIHorizontalLayout"; }
};

class UIGridLayoutComponent final : public UILayoutGroupComponent {
public:
    const char* GetTypeName() const override { return "UIGridLayout"; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

    int columns = 2;
    int rows = 1;
    float gap = 8.0f;
};
