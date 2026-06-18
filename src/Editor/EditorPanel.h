#pragma once

#include <string>

class EditorContext;

class EditorPanel {
public:
    EditorPanel(std::string id, std::string title);
    virtual ~EditorPanel() = default;
    virtual void OnAttach(EditorContext& context) { m_Context = &context; }
    virtual void OnDetach() { m_Context = nullptr; }
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnImGui();
    const std::string& GetID() const { return m_ID; }
    const std::string& GetTitle() const { return m_Title; }
    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool value) { m_Visible = value; }
protected:
    virtual void DrawContent() = 0;
    EditorContext* GetContext() const { return m_Context; }
private:
    std::string m_ID, m_Title;
    bool m_Visible = true;
    EditorContext* m_Context = nullptr;
};
