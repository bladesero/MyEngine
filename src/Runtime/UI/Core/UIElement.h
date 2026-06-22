#pragma once

#include <string>

class UIElement {
public:
    UIElement() = default;
    explicit UIElement(std::string id) : m_ID(std::move(id)) {}

    const std::string& GetID() const { return m_ID; }
    void SetID(std::string id) { m_ID = std::move(id); }

private:
    std::string m_ID;
};
