#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/Asset.h"
#include "Assets/ScriptDiagnostics.h"
#include "Assets/ScriptReflection.h"

#include <memory>
#include <string>
#include <vector>

class ScriptAsset final : public Asset {
public:
    explicit ScriptAsset(const std::string& path);

    const std::string& GetSource() const { return m_Source; }
    const std::vector<ScriptClassInfo>& GetClasses() const { return m_Classes; }
    const std::vector<std::string>& GetDependencies() const { return m_Dependencies; }
    const ScriptDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
    const std::string& GetLastError() const { return m_LastError; }

    void SetSource(std::string source);
    void SetClasses(std::vector<ScriptClassInfo> classes);
    void SetDependencies(std::vector<std::string> dependencies);
    void SetLastError(std::string error);

    bool ReloadFrom(const Asset& source) override;

private:
    std::string m_Source;
    std::vector<ScriptClassInfo> m_Classes;
    std::vector<std::string> m_Dependencies;
    ScriptDiagnostics m_Diagnostics;
    std::string m_LastError;
};

MYENGINE_RUNTIME_API std::shared_ptr<ScriptAsset> LoadScriptAssetFromFile(const std::string& path);

using ScriptAssetPreprocessCallback =
    bool (*)(const std::string&, const std::string&, std::string&, std::vector<std::string>*, std::string&);
using ScriptAssetDiscoverCallback =
    std::vector<ScriptClassInfo> (*)(const std::string&, const std::string&, std::string&);

// Installed by the Runtime composition root. Assets owns the DTO and loading
// policy without taking a dependency on a concrete scripting runtime.
void SetScriptAssetProcessor(ScriptAssetPreprocessCallback preprocess, ScriptAssetDiscoverCallback discover);
