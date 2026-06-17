#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Actor;
class Component;

class ComponentRegistry {
public:
    using Factory = std::function<Component*(Actor&)>;

    static ComponentRegistry& Get();

    bool Register(const std::string& typeName, Factory factory);
    Component* Create(const std::string& typeName, Actor& actor) const;
    bool IsRegistered(const std::string& typeName) const;
    std::vector<std::string> GetRegisteredTypes() const;

private:
    ComponentRegistry();

    std::unordered_map<std::string, Factory> m_Factories;
};
