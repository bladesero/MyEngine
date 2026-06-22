#include "Assets/ScriptAsset.h"

#include "Scripting/AngelScriptRuntime.h"

#include <fstream>
#include <sstream>

ScriptAsset::ScriptAsset(const std::string& path)
    : Asset(AssetType::Script, path)
{
}

void ScriptAsset::SetSource(std::string source)
{
    m_Source = std::move(source);
    SetState(AssetState::Ready);
}

void ScriptAsset::SetClasses(std::vector<ScriptClassInfo> classes)
{
    m_Classes = std::move(classes);
}

void ScriptAsset::SetLastError(std::string error)
{
    m_LastError = std::move(error);
    if (!m_LastError.empty()) SetState(AssetState::Failed);
}

bool ScriptAsset::ReloadFrom(const Asset& source)
{
    const auto* script = dynamic_cast<const ScriptAsset*>(&source);
    if (!script) return false;
    m_Source = script->m_Source;
    m_Classes = script->m_Classes;
    m_LastError = script->m_LastError;
    SetState(script->GetState());
    return true;
}

std::shared_ptr<ScriptAsset> LoadScriptAssetFromFile(const std::string& path)
{
    auto asset = std::make_shared<ScriptAsset>(path);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        asset->SetLastError("failed to open script file: " + path);
        return asset;
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    asset->SetSource(stream.str());

    std::string error;
    asset->SetClasses(AngelScriptRuntime::DiscoverClasses(asset->GetSource(), path, error));
    if (!error.empty()) asset->SetLastError(std::move(error));
    return asset;
}

