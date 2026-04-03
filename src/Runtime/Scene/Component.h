#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Forward declaration
class Actor;

// ==========================================================================
// Component  –  所有组件的基类
//
// 挂载到 Actor 后，生命周期由 Actor 管理。
// ==========================================================================

class Component {
public:
    virtual ~Component() = default;

    // 生命周期回调
    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }

    // 所属 Actor
    Actor* GetOwner() const { return m_Owner; }

    // 组件类型名，子类重写以区分
    virtual const char* GetTypeName() const { return "Component"; }

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

    // 序列化 / 反序列化（子类按需重写）
    // data 是 JSON object，直接向其写入字段
    virtual void Serialize(nlohmann::json& /*data*/) const {}
    virtual void Deserialize(const nlohmann::json& /*data*/) {}

protected:
    Component() = default;

private:
    friend class Actor;
    Actor* m_Owner   = nullptr;
    bool   m_Enabled = true;
};
