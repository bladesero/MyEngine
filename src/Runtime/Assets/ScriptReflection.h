#pragma once

// Script reflection DTOs belong to the Assets module.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

enum class ScriptFieldType {
    Unsupported = 0,
    Bool,
    Int,
    UInt,
    Float,
    Double,
    String,
    Vec2,
    Vec3,
};

struct ScriptFieldInfo {
    std::string name;
    std::string declaration;
    ScriptFieldType type = ScriptFieldType::Unsupported;
    nlohmann::json defaultValue = nlohmann::json();
};

struct ScriptClassInfo {
    std::string name;
    std::vector<ScriptFieldInfo> fields;
};
