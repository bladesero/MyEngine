#pragma once

class UIEventBridge;
class UISystem;

class ScriptBindingContext {
public:
    static ScriptBindingContext& Get();

    void SetUIEventBridge(UIEventBridge* bridge) { m_UIEventBridge = bridge; }
    void ClearUIEventBridge(UIEventBridge* bridge);
    UIEventBridge* GetUIEventBridge() const { return m_UIEventBridge; }

    void SetUISystem(UISystem* system) { m_UISystem = system; }
    void ClearUISystem(UISystem* system);
    UISystem* GetUISystem() const { return m_UISystem; }

private:
    UIEventBridge* m_UIEventBridge = nullptr;
    UISystem* m_UISystem = nullptr;
};
