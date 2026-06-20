#pragma once

#include "Scene/Component.h"
#include "Scripting/ScriptRuntime.h"

#include <filesystem>
#include <memory>
#include <string>

class ScriptComponent final : public Component {
public:
    ScriptComponent() = default;
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

    const char* GetTypeName() const override { return "Script"; }

    void SetSource(std::string source);
    const std::string& GetSource() const { return m_Source; }
    void SetScriptPath(std::string path);
    const std::string& GetScriptPath() const { return m_ScriptPath; }
    const std::string& GetLastError() const { return m_LastError; }
    bool IsCompiled() const { return m_Compiled; }

    const nlohmann::json& GetInspectorFields() const;
    void SetInspectorFields(nlohmann::json fields);
    const nlohmann::json& GetInstanceState() const;

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    void Compile(bool preserveRuntimeState = false);
    bool TryCompileSource(const std::string& source,
                          const std::string& chunkName,
                          nlohmann::json inspector,
                          nlohmann::json state,
                          std::unique_ptr<ScriptRuntime>& outRuntime,
                          nlohmann::json& outInspector,
                          nlohmann::json& outState,
                          std::string& outError);
    void Call(const char* functionName, float deltaSeconds);
    void PollHotReload();
    bool LoadFileSource();
    bool LoadFileSource(std::string& outSource);
    void CaptureRuntimeTables();

    std::string m_Source;
    std::string m_ScriptPath;
    std::string m_LastError;
    nlohmann::json m_Inspector = nlohmann::json::object();
    nlohmann::json m_State = nlohmann::json::object();
    std::unique_ptr<ScriptRuntime> m_Runtime;
    std::filesystem::file_time_type m_LastWriteTime{};
    bool m_HasWriteTime = false;
    bool m_Compiled = false;
    bool m_Awake = false;
    bool m_Started = false;
};
