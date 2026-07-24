#pragma once

// Script diagnostics are serialized/imported asset metadata.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct ScriptDiagnostic {
    enum class Severity { Info, Warning, Error };

    Severity severity = Severity::Error;
    std::string message;
    std::string scriptClass;
    std::string function;
    std::string section;
    int line = 0;
    uint64_t actorHandle = 0;
    std::string actorName;
};

struct ScriptDiagnostics {
    void Clear() { entries.clear(); }
    void Add(ScriptDiagnostic diagnostic) { entries.push_back(std::move(diagnostic)); }
    bool HasErrors() const {
        for (const auto& entry : entries) {
            if (entry.severity == ScriptDiagnostic::Severity::Error)
                return true;
        }
        return false;
    }

    std::vector<ScriptDiagnostic> entries;
};
