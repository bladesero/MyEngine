#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "Physics/CollisionEvent.h"

// Forward declaration
class Actor;
struct AnimationEventData { std::string name; std::string payload; };

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
    virtual void OnInitialize() {}
    virtual void OnBeginPlay() {}
    virtual void OnEnable() {}
    virtual void OnStart() {}
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnFixedUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnLateUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void OnCollisionEvent(const CollisionEvent& event) { (void)event; }
    virtual void OnAnimationEvent(const AnimationEventData& event) { (void)event; }
    virtual void OnDisable() {}
    virtual void OnEndPlay() {}
    virtual void OnDetach() {}

    // 所属 Actor
    Actor* GetOwner() const { return m_Owner; }

    // 组件类型名，子类重写以区分
    virtual const char* GetTypeName() const { return "Component"; }
    virtual int GetExecutionOrder() const { return 0; }

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled);

    // 序列化 / 反序列化（子类按需重写）
    // data 是 JSON object，直接向其写入字段
    virtual void Serialize(nlohmann::json& /*data*/) const {}
    virtual void Deserialize(const nlohmann::json& /*data*/) {}

protected:
    Component() = default;

private:
    friend class Actor;
    friend class Scene;
    Actor* m_Owner   = nullptr;
    bool   m_Enabled = true;
    bool   m_Attached = false;
    bool   m_Initialized = false;
    bool   m_BeganPlay = false;
    bool   m_Started = false;
    bool   m_EffectiveEnabled = false;
};
