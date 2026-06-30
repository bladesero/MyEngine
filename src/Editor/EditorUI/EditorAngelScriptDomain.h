#pragma once

#include "Editor/EditorUI/EditorScriptConfig.h"
#include "Editor/EditorUI/EditorScriptRegistry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class EditorContext;
class asIScriptEngine;
class asIScriptModule;

class EditorAngelScriptDomain {
public:
    EditorAngelScriptDomain();
    ~EditorAngelScriptDomain();

    EditorAngelScriptDomain(const EditorAngelScriptDomain&) = delete;
    EditorAngelScriptDomain& operator=(const EditorAngelScriptDomain&) = delete;

    bool Load(const std::filesystem::path& engineScriptRoot,
              const std::filesystem::path& projectScriptRoot,
              std::string* error = nullptr);
    void SetConfig(EditorScriptConfig config) { m_Config = std::move(config); }
    const EditorScriptConfig& GetConfig() const { return m_Config; }
    bool ReloadIfChanged(std::string* error = nullptr);

    bool IsLoaded() const { return m_Loaded; }
    const std::string& GetLastError() const { return m_LastError; }
    const EditorScriptRegistry& GetRegistry() const { return m_Registry; }

    bool Execute(const std::string& callback, EditorContext& context,
                 std::string* error = nullptr);
    bool ExecuteExtension(const std::string& callback, std::string_view stateKey,
                          EditorContext& context, std::string* error = nullptr);
    bool ExecutePanelBody(std::string_view panelID, EditorContext& context,
                          std::string* error = nullptr);
    bool IsScriptOnlyDebug() const {
        return m_Config.corePanelMode == EditorScriptCorePanelMode::ScriptOnlyDebug;
    }

private:
    struct ScriptSnapshot {
        bool valid = false;
        uint64_t fileCount = 0;
        std::filesystem::file_time_type newestWriteTime{};
    };

    struct RegisterCallback {
        std::string name;
        EditorScriptRegistrationLayer layer = EditorScriptRegistrationLayer::Engine;
    };

    bool CompileFromRoots(const std::filesystem::path& engineScriptRoot,
                          const std::filesystem::path& projectScriptRoot,
                          EditorScriptRegistry& outRegistry,
                          asIScriptEngine*& outEngine,
                          asIScriptModule*& outModule,
                          ScriptSnapshot& outSnapshot,
                          std::string& outError);
    ScriptSnapshot BuildSnapshot(const std::filesystem::path& engineScriptRoot,
                                 const std::filesystem::path& projectScriptRoot) const;
    std::string LoadScripts(const std::filesystem::path& engineScriptRoot,
                            const std::filesystem::path& projectScriptRoot,
                            ScriptSnapshot& snapshot,
                            std::vector<RegisterCallback>* registerCallbacks) const;
    void ReleaseCompiled();

    std::filesystem::path m_EngineScriptRoot;
    std::filesystem::path m_ProjectScriptRoot;
    EditorScriptConfig m_Config;
    EditorScriptRegistry m_Registry;
    asIScriptEngine* m_Engine = nullptr;
    asIScriptModule* m_Module = nullptr;
    ScriptSnapshot m_Snapshot;
    std::string m_LastError;
    bool m_Loaded = false;
};
