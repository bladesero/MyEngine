#pragma once

#include "API/RuntimeApi.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Actor;
class Component;

class MYENGINE_RUNTIME_API ComponentRegistry {
public:
    using Factory = std::function<std::unique_ptr<Component>()>;

    static ComponentRegistry& Get();

    bool Register(const std::string& typeName, Factory factory);
    Component* Create(const std::string& typeName, Actor& actor) const;
    std::unique_ptr<Component> CreateDetached(const std::string& typeName) const;
    bool IsRegistered(const std::string& typeName) const;
    std::vector<std::string> GetRegisteredTypes() const;

private:
    ComponentRegistry();
};
