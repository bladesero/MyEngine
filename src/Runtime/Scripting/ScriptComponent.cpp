#include "Scripting/ScriptComponent.h"

#include <fstream>
#include <sstream>

ScriptComponent::~ScriptComponent() = default;

void ScriptComponent::OnAttach()
{
    Compile();
}

void ScriptComponent::OnDetach()
{
    CaptureRuntimeTables();
    m_Runtime.reset();
}

void ScriptComponent::OnUpdate(float deltaSeconds)
{
    PollHotReload();
    if (!m_Compiled) return;
    if (!m_Awake) {
        Call("Awake", 0.0f);
        m_Awake = true;
    }
    if (!m_Started) {
        Call("Start", 0.0f);
        m_Started = true;
    }
    Call("Update", deltaSeconds);
}

void ScriptComponent::OnFixedUpdate(float deltaSeconds)
{
    if (m_Compiled && m_Started) Call("FixedUpdate", deltaSeconds);
}

void ScriptComponent::OnCollisionEvent(const CollisionEvent& event)
{
    if (!m_Compiled || !m_Started || !m_Runtime) return;
    if (!m_Runtime->CallCollision(event, m_LastError)) m_Compiled = false;
}

void ScriptComponent::SetSource(std::string source)
{
    m_Source = std::move(source);
    m_ScriptPath.clear();
    m_HasWriteTime = false;
    Compile();
}

void ScriptComponent::SetScriptPath(std::string path)
{
    m_ScriptPath = std::move(path);
    LoadFileSource();
    Compile();
}

void ScriptComponent::SetInspectorFields(nlohmann::json fields)
{
    m_Inspector = fields.is_object() ? std::move(fields) : nlohmann::json::object();
    Compile(true);
}

const nlohmann::json& ScriptComponent::GetInspectorFields() const
{
    const_cast<ScriptComponent*>(this)->CaptureRuntimeTables();
    return m_Inspector;
}

const nlohmann::json& ScriptComponent::GetInstanceState() const
{
    const_cast<ScriptComponent*>(this)->CaptureRuntimeTables();
    return m_State;
}

void ScriptComponent::Serialize(nlohmann::json& data) const
{
    auto* self = const_cast<ScriptComponent*>(this);
    self->CaptureRuntimeTables();
    data["source"] = m_Source;
    data["scriptPath"] = m_ScriptPath;
    data["inspector"] = m_Inspector;
    data["state"] = m_State;
}

void ScriptComponent::Deserialize(const nlohmann::json& data)
{
    m_Source = data.value("source", std::string{});
    m_ScriptPath = data.value("scriptPath", std::string{});
    m_Inspector = data.value("inspector", nlohmann::json::object());
    m_State = data.value("state", nlohmann::json::object());
    if (!m_ScriptPath.empty()) LoadFileSource();
    Compile();
}

void ScriptComponent::Compile(bool preserveRuntimeState)
{
    if (preserveRuntimeState) CaptureRuntimeTables();
    m_LastError.clear();
    const std::string chunkName = m_ScriptPath.empty() ? "InlineScript" : m_ScriptPath;
    m_Awake = false;
    m_Started = false;
    std::unique_ptr<ScriptRuntime> runtime;
    nlohmann::json inspector;
    nlohmann::json state;
    std::string error;
    if (TryCompileSource(m_Source, chunkName, m_Inspector, m_State,
                         runtime, inspector, state, error)) {
        m_Runtime = std::move(runtime);
        m_Inspector = std::move(inspector);
        m_State = std::move(state);
        m_Compiled = true;
    } else {
        m_Runtime.reset();
        m_LastError = std::move(error);
        m_Compiled = false;
    }
}

bool ScriptComponent::TryCompileSource(const std::string& source,
                                       const std::string& chunkName,
                                       nlohmann::json inspector,
                                       nlohmann::json state,
                                       std::unique_ptr<ScriptRuntime>& outRuntime,
                                       nlohmann::json& outInspector,
                                       nlohmann::json& outState,
                                       std::string& outError)
{
    auto runtime = std::make_unique<ScriptRuntime>(*this);
    if (!runtime->Load(source, chunkName, inspector, state, outError)) {
        return false;
    }
    outInspector = runtime->CaptureTable("Inspector");
    outState = runtime->CaptureTable("State");
    outRuntime = std::move(runtime);
    return true;
}

void ScriptComponent::Call(const char* functionName, float deltaSeconds)
{
    if (!m_Runtime->Call(functionName, deltaSeconds, m_LastError)) m_Compiled = false;
}

void ScriptComponent::PollHotReload()
{
    if (m_ScriptPath.empty()) return;
    std::error_code error;
    const auto writeTime = std::filesystem::last_write_time(m_ScriptPath, error);
    if (error) return;
    if (!m_HasWriteTime) {
        m_LastWriteTime = writeTime;
        m_HasWriteTime = true;
        return;
    }
    if (writeTime == m_LastWriteTime) return;
    m_LastWriteTime = writeTime;
    CaptureRuntimeTables();
    std::string source;
    if (!LoadFileSource(source)) return;

    std::unique_ptr<ScriptRuntime> runtime;
    nlohmann::json inspector;
    nlohmann::json state;
    std::string compileError;
    if (!TryCompileSource(source, m_ScriptPath, m_Inspector, m_State,
                          runtime, inspector, state, compileError)) {
        m_LastError = std::move(compileError);
        return;
    }

    m_Source = std::move(source);
    m_Runtime = std::move(runtime);
    m_Inspector = std::move(inspector);
    m_State = std::move(state);
    m_Compiled = true;
    m_Awake = false;
    m_Started = false;
}

bool ScriptComponent::LoadFileSource()
{
    return LoadFileSource(m_Source);
}

bool ScriptComponent::LoadFileSource(std::string& outSource)
{
    std::ifstream input(m_ScriptPath, std::ios::binary);
    if (!input) {
        m_LastError = "failed to open script file: " + m_ScriptPath;
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    outSource = stream.str();
    std::error_code error;
    m_LastWriteTime = std::filesystem::last_write_time(m_ScriptPath, error);
    m_HasWriteTime = !error;
    return true;
}

void ScriptComponent::CaptureRuntimeTables()
{
    if (!m_Runtime || !m_Compiled) return;
    m_Inspector = m_Runtime->CaptureTable("Inspector");
    m_State = m_Runtime->CaptureTable("State");
}
