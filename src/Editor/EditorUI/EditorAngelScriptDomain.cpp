#include "Editor/EditorUI/EditorAngelScriptDomain.h"

#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorUI/EditorUIFacade.h"

#include <angelscript.h>
#include <scriptstdstring.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
bool Check(int result) {
    return result >= 0;
}

struct MessageSink {
    std::ostringstream stream;
    static void Callback(const asSMessageInfo* message, void* user) {
        auto* sink = static_cast<MessageSink*>(user);
        if (!sink || !message)
            return;
        sink->stream << message->section << "(" << message->row << "," << message->col << ") ";
        if (message->type == asMSGTYPE_ERROR)
            sink->stream << "error: ";
        else if (message->type == asMSGTYPE_WARNING)
            sink->stream << "warning: ";
        else
            sink->stream << "info: ";
        sink->stream << message->message << "\n";
    }
};

void RegisterRegistryBindings(asIScriptEngine& engine) {
    Check(engine.RegisterEnum("PanelArea"));
    Check(engine.RegisterEnumValue("PanelArea", "Top", static_cast<int>(EditorScriptPanelArea::Top)));
    Check(engine.RegisterEnumValue("PanelArea", "Left", static_cast<int>(EditorScriptPanelArea::Left)));
    Check(engine.RegisterEnumValue("PanelArea", "Center", static_cast<int>(EditorScriptPanelArea::Center)));
    Check(engine.RegisterEnumValue("PanelArea", "Right", static_cast<int>(EditorScriptPanelArea::Right)));
    Check(engine.RegisterEnumValue("PanelArea", "BottomLeft", static_cast<int>(EditorScriptPanelArea::BottomLeft)));
    Check(engine.RegisterEnumValue("PanelArea", "BottomCenter", static_cast<int>(EditorScriptPanelArea::BottomCenter)));

    Check(engine.RegisterObjectType("EditorRegistry", 0, asOBJ_REF | asOBJ_NOCOUNT));
    Check(engine.RegisterObjectMethod("EditorRegistry",
                                      "void Panel(const string &in, const string &in, int, const string &in)",
                                      asMETHOD(EditorScriptRegistry, Panel), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry",
                                      "void ToolPanel(const string &in, const string &in, int, const string &in)",
                                      asMETHOD(EditorScriptRegistry, ToolPanel), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void PanelBody(const string &in, const string &in)",
                                      asMETHOD(EditorScriptRegistry, PanelBody), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void Menu(const string &in, const string &in)",
                                      asMETHOD(EditorScriptRegistry, Menu), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void MenuItem(const string &in, const string &in)",
                                      asMETHOD(EditorScriptRegistry, MenuItem), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void ToolbarItem(const string &in, int, const string &in)",
                                      asMETHOD(EditorScriptRegistry, ToolbarItem), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void Inspector(const string &in, int, const string &in)",
                                      asMETHOD(EditorScriptRegistry, Inspector), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry",
                                      "void InspectorSection(const string &in, int, const string &in)",
                                      asMETHOD(EditorScriptRegistry, InspectorSection), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void AssetContextMenu(const string &in, const string &in)",
                                      asMETHOD(EditorScriptRegistry, AssetContextMenu), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("EditorRegistry", "void ActorContextMenu(const string &in)",
                                      asMETHOD(EditorScriptRegistry, ActorContextMenu), asCALL_THISCALL));
}

std::vector<std::filesystem::path> CollectScriptFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root, ec) || ec)
        return files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (entry.path().extension() == ".as")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string RewriteRegisterEditor(std::string source, const std::string& replacementName, bool* replaced) {
    const char* patterns[] = {
        "void RegisterEditor(EditorRegistry@ registry)",
        "void RegisterEditor(EditorRegistry @ registry)",
        "void RegisterEditor(EditorRegistry@registry)",
        "void RegisterEditor(EditorRegistry @registry)",
    };
    for (const char* pattern : patterns) {
        const size_t found = source.find(pattern);
        if (found == std::string::npos)
            continue;
        const std::string replacement = "void " + replacementName + "(EditorRegistry@ registry)";
        source.replace(found, std::strlen(pattern), replacement);
        if (replaced)
            *replaced = true;
        return source;
    }
    if (replaced)
        *replaced = false;
    return source;
}
} // namespace

EditorAngelScriptDomain::EditorAngelScriptDomain() = default;

EditorAngelScriptDomain::~EditorAngelScriptDomain() {
    ReleaseCompiled();
}

void EditorAngelScriptDomain::ReleaseCompiled() {
    m_Module = nullptr;
    if (m_Engine) {
        m_Engine->ShutDownAndRelease();
        m_Engine = nullptr;
    }
}

std::string EditorAngelScriptDomain::LoadScripts(const std::filesystem::path& engineScriptRoot,
                                                 const std::filesystem::path& projectScriptRoot,
                                                 ScriptSnapshot& snapshot,
                                                 std::vector<RegisterCallback>* registerCallbacks) const {
    snapshot = {};
    std::ostringstream source;
    const auto appendFiles = [&](const std::filesystem::path& root, const char* layer,
                                 EditorScriptRegistrationLayer registrationLayer) {
        for (const auto& path : CollectScriptFiles(root)) {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                continue;
            std::ostringstream fileBuffer;
            fileBuffer << input.rdbuf();
            const std::string callbackName = "__RegisterEditor_" + std::to_string(snapshot.fileCount);
            bool hasRegisterEditor = false;
            std::string fileSource = RewriteRegisterEditor(fileBuffer.str(), callbackName, &hasRegisterEditor);
            if (hasRegisterEditor && registerCallbacks) {
                registerCallbacks->push_back({callbackName, registrationLayer});
            }
            source << "\n// " << layer << ": " << path.generic_string() << "\n";
            source << fileSource << "\n";
            ++snapshot.fileCount;
            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(path, ec);
            if (!ec && writeTime > snapshot.newestWriteTime)
                snapshot.newestWriteTime = writeTime;
        }
    };
    appendFiles(engineScriptRoot, "engine", EditorScriptRegistrationLayer::Engine);
    appendFiles(projectScriptRoot, "project", EditorScriptRegistrationLayer::Project);
    snapshot.valid = snapshot.fileCount > 0;
    return source.str();
}

EditorAngelScriptDomain::ScriptSnapshot
EditorAngelScriptDomain::BuildSnapshot(const std::filesystem::path& engineScriptRoot,
                                       const std::filesystem::path& projectScriptRoot) const {
    ScriptSnapshot snapshot;
    const auto scan = [&](const std::filesystem::path& root) {
        for (const auto& path : CollectScriptFiles(root)) {
            ++snapshot.fileCount;
            std::error_code ec;
            const auto writeTime = std::filesystem::last_write_time(path, ec);
            if (!ec && writeTime > snapshot.newestWriteTime)
                snapshot.newestWriteTime = writeTime;
        }
    };
    scan(engineScriptRoot);
    scan(projectScriptRoot);
    snapshot.valid = snapshot.fileCount > 0;
    return snapshot;
}

bool EditorAngelScriptDomain::CompileFromRoots(const std::filesystem::path& engineScriptRoot,
                                               const std::filesystem::path& projectScriptRoot,
                                               EditorScriptRegistry& outRegistry, asIScriptEngine*& outEngine,
                                               asIScriptModule*& outModule, ScriptSnapshot& outSnapshot,
                                               std::string& outError) {
    outError.clear();
    outRegistry.Clear();
    outRegistry.SetAllowProjectAppend(m_Config.allowProjectAppend);
    outRegistry.SetAllowProjectOverrideCore(m_Config.allowProjectOverrideCore);
    outEngine = nullptr;
    outModule = nullptr;

    std::vector<RegisterCallback> registerCallbacks;
    const std::string source = LoadScripts(engineScriptRoot, projectScriptRoot, outSnapshot, &registerCallbacks);
    if (source.empty()) {
        outError = "no editor AngelScript files found";
        return false;
    }

    MessageSink messages;
    asIScriptEngine* engine = asCreateScriptEngine();
    if (!engine) {
        outError = "failed to create editor AngelScript engine";
        return false;
    }
    engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
    engine->SetMessageCallback(asFUNCTION(MessageSink::Callback), &messages, asCALL_CDECL);
    RegisterStdString(engine);
    RegisterRegistryBindings(*engine);
    Editor::Scripting::RegisterEditorUIFacade(engine);
    Editor::Scripting::RegisterEditorContextBindings(engine);

    asIScriptModule* module = engine->GetModule("EditorDomain", asGM_ALWAYS_CREATE);
    if (!module) {
        engine->ShutDownAndRelease();
        outError = "failed to create editor AngelScript module";
        return false;
    }

    int result = module->AddScriptSection("EditorScripts", source.data(), static_cast<unsigned int>(source.size()));
    if (result < 0 || module->Build() < 0) {
        outError = messages.stream.str();
        if (outError.empty())
            outError = "editor AngelScript compile failed";
        engine->ShutDownAndRelease();
        return false;
    }

    if (registerCallbacks.empty()) {
        engine->ShutDownAndRelease();
        outError = "editor AngelScript entry point missing: void RegisterEditor(EditorRegistry@)";
        return false;
    }

    asIScriptContext* context = engine->CreateContext();
    if (!context) {
        engine->ShutDownAndRelease();
        outError = "failed to create editor AngelScript register context";
        return false;
    }
    for (const RegisterCallback& callback : registerCallbacks) {
        asIScriptFunction* registerEditor = module->GetFunctionByName(callback.name.c_str());
        if (!registerEditor) {
            context->Release();
            engine->ShutDownAndRelease();
            outError = "editor AngelScript entry point was not compiled: " + callback.name;
            return false;
        }
        outRegistry.SetRegistrationLayer(callback.layer);
        context->Prepare(registerEditor);
        context->SetArgObject(0, &outRegistry);
        result = context->Execute();
        if (result != asEXECUTION_FINISHED) {
            std::ostringstream error;
            error << "editor AngelScript RegisterEditor failed";
            if (result == asEXECUTION_EXCEPTION) {
                error << ": " << context->GetExceptionString();
                if (asIScriptFunction* fn = context->GetExceptionFunction()) {
                    error << " at " << fn->GetDeclaration() << ":" << context->GetExceptionLineNumber();
                }
            }
            context->Release();
            engine->ShutDownAndRelease();
            outError = error.str();
            return false;
        }
    }
    context->Release();

    outEngine = engine;
    outModule = module;
    return true;
}

bool EditorAngelScriptDomain::Load(const std::filesystem::path& engineScriptRoot,
                                   const std::filesystem::path& projectScriptRoot, std::string* error) {
    m_EngineScriptRoot = engineScriptRoot;
    m_ProjectScriptRoot = projectScriptRoot;

    EditorScriptRegistry candidateRegistry;
    asIScriptEngine* candidateEngine = nullptr;
    asIScriptModule* candidateModule = nullptr;
    ScriptSnapshot candidateSnapshot;
    std::string compileError;
    if (!CompileFromRoots(engineScriptRoot, projectScriptRoot, candidateRegistry, candidateEngine, candidateModule,
                          candidateSnapshot, compileError)) {
        m_LastError = compileError;
        if (error)
            *error = m_LastError;
        Logger::Warn("[EditorScript] ", m_LastError);
        return false;
    }

    ReleaseCompiled();
    m_Registry = std::move(candidateRegistry);
    m_Engine = candidateEngine;
    m_Module = candidateModule;
    m_Snapshot = candidateSnapshot;
    m_LastError.clear();
    m_Loaded = true;
    for (const std::string& diagnostic : m_Registry.GetDiagnostics()) {
        Logger::Warn("[EditorScript] ", diagnostic);
    }
    if (error)
        error->clear();
    return true;
}

bool EditorAngelScriptDomain::Reload(std::string* error) {
    return Load(m_EngineScriptRoot, m_ProjectScriptRoot, error);
}

bool EditorAngelScriptDomain::ReloadIfChanged(std::string* error) {
    const ScriptSnapshot current = BuildSnapshot(m_EngineScriptRoot, m_ProjectScriptRoot);
    if (current.valid == m_Snapshot.valid && current.fileCount == m_Snapshot.fileCount &&
        current.newestWriteTime == m_Snapshot.newestWriteTime) {
        if (error)
            error->clear();
        return true;
    }
    return Load(m_EngineScriptRoot, m_ProjectScriptRoot, error);
}

bool EditorAngelScriptDomain::Execute(const std::string& callback, EditorContext& context, std::string* error) {
    if (error)
        error->clear();
    if (!m_Loaded || !m_Engine || !m_Module || callback.empty())
        return false;
    asIScriptFunction* function = m_Module->GetFunctionByName(callback.c_str());
    if (!function) {
        if (error)
            *error = "editor AngelScript callback not found: " + callback;
        return false;
    }

    asIScriptContext* scriptContext = m_Engine->CreateContext();
    if (!scriptContext) {
        if (error)
            *error = "failed to create editor AngelScript draw context";
        return false;
    }
    scriptContext->Prepare(function);
    Editor::Scripting::SetActiveEditorContext(&context);
    const int result = scriptContext->Execute();
    Editor::Scripting::SetActiveEditorContext(nullptr);
    if (result != asEXECUTION_FINISHED) {
        std::ostringstream stream;
        stream << "editor AngelScript callback failed: " << callback;
        if (result == asEXECUTION_EXCEPTION) {
            stream << ": " << scriptContext->GetExceptionString();
            if (asIScriptFunction* fn = scriptContext->GetExceptionFunction()) {
                stream << " at " << fn->GetDeclaration() << ":" << scriptContext->GetExceptionLineNumber();
            }
        }
        if (error)
            *error = stream.str();
        scriptContext->Release();
        return false;
    }
    scriptContext->Release();
    return true;
}

bool EditorAngelScriptDomain::ExecuteExtension(const std::string& callback, std::string_view stateKey,
                                               EditorContext& context, std::string* error) {
    const std::string key(stateKey);
    Editor::Scripting::SetActiveEditorPanelID(key);
    const bool executed = Execute(callback, context, error);
    Editor::Scripting::ClearActiveEditorPanelID();
    return executed;
}

bool EditorAngelScriptDomain::ExecutePanelBody(std::string_view panelID, EditorContext& context, std::string* error) {
    if (error)
        error->clear();
    if (!m_Config.enabled || m_Config.corePanelMode == EditorScriptCorePanelMode::CppOnly) {
        return false;
    }

    const std::string panelKey(panelID);
    if (!m_Config.IsCorePanelEnabled(panelKey))
        return false;

    const std::string* callback = m_Registry.FindPanelBodyCallback(panelKey);
    if (!callback || callback->empty())
        return false;
    return ExecuteExtension(*callback, panelKey, context, error);
}
