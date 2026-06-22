#include "Scripting/ScriptComponent.h"

#include "Assets/AssetManager.h"

#include <fstream>
#include <sstream>

namespace {

constexpr const char* kDefaultAngelScriptSource =
    "class Script {\n"
    "  void Awake() {}\n"
    "  void OnEnable() {}\n"
    "  void Start() {}\n"
    "  void FixedUpdate(float dt) {}\n"
    "  void Update(float dt) {}\n"
    "  void LateUpdate(float dt) {}\n"
    "  void OnCollision(const CollisionEvent &in event) {}\n"
    "  void OnDisable() {}\n"
    "  void OnDestroy() {}\n"
    "}\n";

} // namespace

ScriptComponent::ScriptComponent()
    : m_Source(kDefaultAngelScriptSource)
{
}

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

void ScriptComponent::OnBeginPlay()
{
    if (m_Compiled && !m_Awake) { Call("Awake", 0.0f); m_Awake = true; }
}

void ScriptComponent::OnEnable()
{
    if (m_Compiled && m_Awake) Call("OnEnable", 0.0f);
}

void ScriptComponent::OnStart()
{
    if (m_Compiled && !m_Started) { Call("Start", 0.0f); m_Started = true; }
}

void ScriptComponent::OnUpdate(float deltaSeconds)
{
    PollHotReload();
    if (!m_Compiled) return;
    Call("Update", deltaSeconds);
}

void ScriptComponent::OnFixedUpdate(float deltaSeconds)
{
    if (m_Compiled && m_Started) Call("FixedUpdate", deltaSeconds);
}

void ScriptComponent::OnLateUpdate(float deltaSeconds)
{
    if (m_Compiled && m_Started) Call("LateUpdate", deltaSeconds);
}

void ScriptComponent::OnDisable()
{
    if (m_Compiled && m_Awake) Call("OnDisable", 0.0f);
}

void ScriptComponent::OnEndPlay()
{
    if (m_Compiled && m_Awake) Call("OnDestroy", 0.0f);
}

void ScriptComponent::OnCollisionEvent(const CollisionEvent& event)
{
    if (!m_Compiled || !m_Started || !m_Runtime) return;
    if (!m_Runtime->CallCollision(event, m_LastError)) m_Compiled = false;
}

void ScriptComponent::SetSource(std::string source)
{
    m_Language = "angelscript";
    m_LegacyLua = false;
    m_Source = std::move(source);
    m_ScriptPath.clear();
    m_HasWriteTime = false;
    Compile();
}

void ScriptComponent::SetScriptPath(std::string path)
{
    m_Language = "angelscript";
    m_LegacyLua = false;
    m_ScriptPath = std::move(path);
    LoadFileSource();
    Compile();
}

void ScriptComponent::SetClassName(std::string className)
{
    m_ClassName = className.empty() ? "Script" : std::move(className);
    Compile(true);
}

void ScriptComponent::SetInspectorFields(nlohmann::json fields)
{
    SetProperties(std::move(fields));
}

void ScriptComponent::SetProperties(nlohmann::json properties)
{
    m_Inspector = properties.is_object() ? std::move(properties) : nlohmann::json::object();
    Compile(false);
}

bool ScriptComponent::SetPropertyValue(const std::string& name, nlohmann::json value)
{
    bool known = false;
    for (const auto& field : m_Fields) {
        if (field.name == name) {
            known = true;
            break;
        }
    }
    if (!known) return false;
    nlohmann::json next = GetProperties();
    next[name] = std::move(value);
    SetProperties(std::move(next));
    return m_Compiled;
}

const nlohmann::json& ScriptComponent::GetInspectorFields() const
{
    return GetProperties();
}

const nlohmann::json& ScriptComponent::GetProperties() const
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
    if (m_LegacyLua) {
        data["source"] = m_Source;
        data["scriptPath"] = AssetManager::Get().MakeProjectRelativePath(m_ScriptPath);
        data["inspector"] = m_LegacyInspector;
        data["state"] = m_LegacyState;
        return;
    }
    data["language"] = "angelscript";
    data["className"] = m_ClassName.empty() ? "Script" : m_ClassName;
    data["scriptPath"] = AssetManager::Get().MakeProjectRelativePath(m_ScriptPath);
    if (m_ScriptPath.empty()) data["source"] = m_Source;
    data["properties"] = m_Inspector;
    data["state"] = m_State;
}

void ScriptComponent::Deserialize(const nlohmann::json& data)
{
    m_Language = data.value("language", std::string{});
    m_LegacyLua = m_Language.empty();
    if (m_LegacyLua) {
        m_Language = "lua";
        m_ClassName = "Script";
        m_Source = data.value("source", std::string{});
        const std::string storedPath = data.value("scriptPath", std::string{});
        m_ScriptPath = storedPath.empty()
            ? std::string{}
            : AssetManager::Get().ResolvePath(storedPath);
        m_LegacyInspector = data.value("inspector", nlohmann::json::object());
        m_LegacyState = data.value("state", nlohmann::json::object());
        m_Inspector = m_LegacyInspector;
        m_State = m_LegacyState;
        if (!m_ScriptPath.empty()) LoadFileSource();
        MarkLegacyLua();
        return;
    }

    m_Language = "angelscript";
    m_LegacyLua = false;
    m_ClassName = data.value("className", std::string{"Script"});
    if (m_ClassName.empty()) m_ClassName = "Script";
    m_Source = data.value("source", std::string{});
    const std::string storedPath = data.value("scriptPath", std::string{});
    m_ScriptPath = storedPath.empty()
        ? std::string{}
        : AssetManager::Get().ResolvePath(storedPath);
    m_Inspector = data.value("properties", data.value("inspector", nlohmann::json::object()));
    m_State = data.value("state", nlohmann::json::object());
    if (!m_ScriptPath.empty()) LoadFileSource();
    Compile();
}

void ScriptComponent::Compile(bool preserveRuntimeState)
{
    if (m_LegacyLua || m_Language != "angelscript") {
        MarkLegacyLua();
        return;
    }
    if (preserveRuntimeState) CaptureRuntimeTables();
    m_LastError.clear();
    const std::string chunkName = m_ScriptPath.empty() ? "InlineScript" : m_ScriptPath;
    m_Awake = false;
    m_Started = false;
    std::unique_ptr<AngelScriptRuntime> runtime;
    nlohmann::json inspector;
    nlohmann::json state;
    std::string error;
    if (TryCompileSource(m_Source, chunkName, m_ClassName, m_Inspector, m_State,
                         runtime, inspector, state, error)) {
        m_Runtime = std::move(runtime);
        m_Inspector = std::move(inspector);
        m_State = std::move(state);
        m_Fields = m_Runtime->GetFields();
        m_Compiled = true;
    } else {
        m_Runtime.reset();
        m_Fields.clear();
        m_LastError = std::move(error);
        m_Compiled = false;
    }
}

bool ScriptComponent::TryCompileSource(const std::string& source,
                                       const std::string& chunkName,
                                       const std::string& className,
                                       nlohmann::json inspector,
                                       nlohmann::json state,
                                       std::unique_ptr<AngelScriptRuntime>& outRuntime,
                                       nlohmann::json& outInspector,
                                       nlohmann::json& outState,
                                       std::string& outError)
{
    auto runtime = std::make_unique<AngelScriptRuntime>(*this);
    if (!runtime->Load(source, chunkName, className, inspector, state, outError)) {
        return false;
    }
    outInspector = runtime->GetProperties();
    outState = runtime->GetState();
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

    nlohmann::json inspector;
    nlohmann::json state;
    std::string compileError;
    std::unique_ptr<AngelScriptRuntime> runtime;
    if (!TryCompileSource(source, m_ScriptPath, m_ClassName, m_Inspector, m_State,
                          runtime, inspector, state, compileError)) {
        m_LastError = std::move(compileError);
        return;
    }

    m_Source = std::move(source);
    m_Runtime = std::move(runtime);
    m_Inspector = std::move(inspector);
    m_State = std::move(state);
    m_Fields = m_Runtime->GetFields();
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
    m_Inspector = m_Runtime->GetProperties();
    m_State = m_Runtime->GetState();
}

void ScriptComponent::MarkLegacyLua()
{
    m_Runtime.reset();
    m_Fields.clear();
    m_Compiled = false;
    m_Awake = false;
    m_Started = false;
    m_LastError = "Legacy Lua gameplay scripts are no longer executed by Runtime";
}
