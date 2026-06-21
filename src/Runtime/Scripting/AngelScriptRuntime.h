#pragma once

#include "Physics/CollisionEvent.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

class ScriptComponent;

class AngelScriptRuntime {
public:
    explicit AngelScriptRuntime(ScriptComponent& component);
    ~AngelScriptRuntime();

    AngelScriptRuntime(const AngelScriptRuntime&) = delete;
    AngelScriptRuntime& operator=(const AngelScriptRuntime&) = delete;

    bool Load(const std::string& source, const std::string& chunkName,
              const std::string& className, const nlohmann::json& properties,
              const nlohmann::json& state, std::string& error);
    bool Call(const char* methodDecl, float deltaSeconds, std::string& error);
    bool CallCollision(const CollisionEvent& event, std::string& error);

    const nlohmann::json& GetProperties() const;
    const nlohmann::json& GetState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
