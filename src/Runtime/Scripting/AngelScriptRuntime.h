#pragma once

#include "Physics/CollisionEvent.h"
#include "Scripting/ScriptReflection.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class ScriptComponent;
class UIEventBridge;
class UISystem;

class AngelScriptRuntime {
public:
    explicit AngelScriptRuntime(ScriptComponent& component);
    ~AngelScriptRuntime();

    AngelScriptRuntime(const AngelScriptRuntime&) = delete;
    AngelScriptRuntime& operator=(const AngelScriptRuntime&) = delete;

    bool Load(const std::string& source, const std::string& chunkName, const std::string& className,
              const nlohmann::json& properties, const nlohmann::json& state, std::string& error);
    bool Call(const char* methodDecl, float deltaSeconds, std::string& error);
    bool CallCollision(const CollisionEvent& event, std::string& error);
    bool CallAnimationEvent(const std::string& name, const std::string& payload, std::string& error);
    bool SubscribeUIEvent(const std::string& elementId, const std::string& eventName, const std::string& callbackName,
                          std::string& error);
    void UnsubscribeUIEvent(const std::string& elementId, const std::string& eventName);
    void ClearUIEventSubscriptions();
    bool SubscribeScriptEvent(const std::string& eventName, const std::string& callbackName, std::string& error);
    bool EmitScriptEvent(const std::string& eventName, const std::string& jsonPayload);
    void ClearScriptEventSubscriptions();
    uint64_t ScheduleTimer(float seconds, bool repeat, const std::string& callbackName, std::string& error);
    void CancelTimer(uint64_t timerID);
    void CancelAllTimers();
    bool TickServices(float deltaSeconds, std::string& error);

    const nlohmann::json& GetProperties() const;
    const nlohmann::json& GetState() const;
    const std::vector<ScriptFieldInfo>& GetFields() const;

    static std::vector<ScriptClassInfo> DiscoverClasses(const std::string& source, const std::string& chunkName,
                                                        std::string& error);
    static bool PreprocessSource(const std::string& source, const std::string& chunkName, std::string& outSource,
                                 std::vector<std::string>* outDependencies, std::string& error);
    static void SetUIEventBridge(UIEventBridge* bridge);
    static void ClearUIEventBridge(UIEventBridge* bridge);
    static void SetUISystem(UISystem* system);
    static void ClearUISystem(UISystem* system);

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
