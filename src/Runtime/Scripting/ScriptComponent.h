#pragma once

#include "Scene/Component.h"
#include "Scripting/AngelScriptRuntime.h"
#include "Scripting/ScriptDiagnostics.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ScriptComponent final : public Component {
public:
    ScriptComponent();
    ~ScriptComponent() override;

    void OnAttach() override;
    void OnBeginPlay() override;
    void OnEnable() override;
    void OnStart() override;
    void OnDetach() override;
    void OnUpdate(float deltaSeconds) override;
    void OnFixedUpdate(float deltaSeconds) override;
    void OnLateUpdate(float deltaSeconds) override;
    void OnDisable() override;
    void OnEndPlay() override;
    void OnCollisionEvent(const CollisionEvent& event) override;
    void OnAnimationEvent(const AnimationEventData& event) override;

    const char* GetTypeName() const override { return "Script"; }

    void SetSource(std::string source);
    const std::string& GetSource() const { return m_Source; }
    void SetScriptPath(std::string path);
    const std::string& GetScriptPath() const { return m_ScriptPath; }
    void SetClassName(std::string className);
    const std::string& GetClassName() const { return m_ClassName; }
    const std::string& GetLanguage() const { return m_Language; }
    const std::string& GetLastError() const { return m_LastError; }
    const ScriptDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
    bool IsCompiled() const { return m_Compiled; }
    bool IsLegacyLua() const { return m_LegacyLua; }

    const nlohmann::json& GetInspectorFields() const;
    void SetInspectorFields(nlohmann::json fields);
    const nlohmann::json& GetProperties() const;
    void SetProperties(nlohmann::json properties);
    const std::vector<ScriptFieldInfo>& GetFields() const { return m_Fields; }
    bool SetPropertyValue(const std::string& name, nlohmann::json value);
    const nlohmann::json& GetInstanceState() const;
    bool SubscribeUIEvent(const std::string& elementId, const std::string& eventName, const std::string& callbackName);
    void UnsubscribeUIEvent(const std::string& elementId, const std::string& eventName);
    void ClearUIEventSubscriptions();
    bool SubscribeScriptEvent(const std::string& eventName, const std::string& callbackName);
    void ClearScriptEventSubscriptions();
    bool EmitScriptEvent(const std::string& eventName, const std::string& jsonPayload);
    uint64_t ScheduleTimer(float seconds, bool repeat, const std::string& callbackName);
    void CancelTimer(uint64_t timerID);
    void CancelAllTimers();
    void FailRuntime(std::string error);
    void AddDiagnostic(ScriptDiagnostic diagnostic);

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    void Compile(bool preserveRuntimeState = false);
    bool TryCompileSource(const std::string& source, const std::string& chunkName, const std::string& className,
                          nlohmann::json inspector, nlohmann::json state,
                          std::unique_ptr<AngelScriptRuntime>& outRuntime, nlohmann::json& outInspector,
                          nlohmann::json& outState, std::vector<std::string>& outDependencies, std::string& outError);
    void Call(const char* functionName, float deltaSeconds);
    void PollHotReload();
    bool LoadFileSource();
    bool LoadFileSource(std::string& outSource);
    std::filesystem::file_time_type CurrentDependencyWriteTime(bool& valid) const;
    void CaptureRuntimeTables();
    void MarkLegacyLua();

    std::string m_Language = "angelscript";
    std::string m_ClassName = "Script";
    std::string m_Source;
    std::string m_ScriptPath;
    std::string m_LastError;
    ScriptDiagnostics m_Diagnostics;
    nlohmann::json m_Inspector = nlohmann::json::object();
    nlohmann::json m_State = nlohmann::json::object();
    nlohmann::json m_LegacyInspector = nlohmann::json::object();
    nlohmann::json m_LegacyState = nlohmann::json::object();
    std::vector<ScriptFieldInfo> m_Fields;
    std::unique_ptr<AngelScriptRuntime> m_Runtime;
    std::vector<std::string> m_IncludeDependencies;
    std::filesystem::file_time_type m_LastWriteTime{};
    bool m_HasWriteTime = false;
    bool m_Compiled = false;
    bool m_LegacyLua = false;
    bool m_Awake = false;
    bool m_Started = false;
};
