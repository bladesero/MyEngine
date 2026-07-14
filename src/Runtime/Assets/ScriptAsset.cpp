#include "Assets/ScriptAsset.h"

#include "Core/RuntimeFileSystem.h"
#include "Scripting/AngelScriptRuntime.h"

#include <fstream>
#include <sstream>

ScriptAsset::ScriptAsset(const std::string& path) : Asset(AssetType::Script, path) {
}

void ScriptAsset::SetSource(std::string source) {
    m_Source = std::move(source);
    SetState(AssetState::Ready);
}

void ScriptAsset::SetClasses(std::vector<ScriptClassInfo> classes) {
    m_Classes = std::move(classes);
}

void ScriptAsset::SetDependencies(std::vector<std::string> dependencies) {
    m_Dependencies = std::move(dependencies);
}

void ScriptAsset::SetLastError(std::string error) {
    m_LastError = std::move(error);
    m_Diagnostics.Clear();
    if (!m_LastError.empty()) {
        ScriptDiagnostic diagnostic;
        diagnostic.severity = ScriptDiagnostic::Severity::Error;
        diagnostic.message = m_LastError;
        diagnostic.section = GetPath();
        m_Diagnostics.Add(std::move(diagnostic));
    }
    if (!m_LastError.empty())
        SetState(AssetState::Failed);
}

bool ScriptAsset::ReloadFrom(const Asset& source) {
    const auto* script = dynamic_cast<const ScriptAsset*>(&source);
    if (!script)
        return false;
    m_Source = script->m_Source;
    m_Classes = script->m_Classes;
    m_Dependencies = script->m_Dependencies;
    m_Diagnostics = script->m_Diagnostics;
    m_LastError = script->m_LastError;
    SetState(script->GetState());
    return true;
}

std::shared_ptr<ScriptAsset> LoadScriptAssetFromFile(const std::string& path) {
    auto asset = std::make_shared<ScriptAsset>(path);
    std::string source;
    if (!RuntimeFileSystem::Get().ReadText(path, source)) {
        asset->SetLastError("failed to open script file: " + path);
        return asset;
    }
    asset->SetSource(std::move(source));

    std::string error;
    std::string expandedSource;
    std::vector<std::string> dependencies;
    if (!AngelScriptRuntime::PreprocessSource(asset->GetSource(), path, expandedSource, &dependencies, error)) {
        asset->SetLastError(std::move(error));
        return asset;
    }
    asset->SetDependencies(std::move(dependencies));
    asset->SetClasses(AngelScriptRuntime::DiscoverClasses(expandedSource, path, error));
    if (!error.empty())
        asset->SetLastError(std::move(error));
    return asset;
}
