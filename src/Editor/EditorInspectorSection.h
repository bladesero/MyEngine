#pragma once

#include <algorithm>
#include <memory>
#include <vector>

class EditorContext;
class EditorSelection;

class EditorInspectorSection {
public:
    virtual ~EditorInspectorSection() = default;
    virtual const char* GetID() const = 0;
    virtual int GetOrder() const { return 0; }
    virtual bool CanDraw(const EditorSelection& selection) const = 0;
    virtual void Draw(EditorContext& context) = 0;
};

class EditorInspectorRegistry {
public:
    void Register(std::unique_ptr<EditorInspectorSection> section)
    {
        if (!section) return;
        m_Sections.push_back(std::move(section));
        std::stable_sort(m_Sections.begin(), m_Sections.end(),
            [](const auto& left, const auto& right) {
                return left->GetOrder() < right->GetOrder();
            });
    }

    const std::vector<std::unique_ptr<EditorInspectorSection>>& GetSections() const
    {
        return m_Sections;
    }

private:
    std::vector<std::unique_ptr<EditorInspectorSection>> m_Sections;
};
