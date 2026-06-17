#pragma once

#include "Physics/CollisionEvent.h"

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

class ScriptComponent;

class ScriptRuntime {
public:
    explicit ScriptRuntime(ScriptComponent& component);
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    bool Load(const std::string& source, const std::string& chunkName,
              const nlohmann::json& inspector, const nlohmann::json& state,
              std::string& error);
    bool Call(const char* functionName, float deltaSeconds, std::string& error);
    bool CallCollision(const CollisionEvent& event, std::string& error);

    nlohmann::json CaptureTable(const char* tableName) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
