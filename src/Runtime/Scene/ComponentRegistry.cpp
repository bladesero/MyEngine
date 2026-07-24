#include "ComponentRegistry.h"

#include "Actor.h"
#include "TypeRegistry.h"

#include <typeindex>
#include <utility>

ComponentRegistry& ComponentRegistry::Get() {
    static ComponentRegistry registry;
    return registry;
}

ComponentRegistry::ComponentRegistry() = default;

bool ComponentRegistry::Register(const std::string& typeName, Factory factory) {
    if (typeName.empty() || !factory)
        return false;
    TypeDescriptor descriptor;
    descriptor.stableName = typeName;
    descriptor.factory = std::move(factory);
    auto probe = descriptor.factory();
    descriptor.cppType = probe ? std::type_index(typeid(*probe)) : std::type_index(typeid(void));
    return TypeRegistry::Get().Register(std::move(descriptor));
}

Component* ComponentRegistry::Create(const std::string& typeName, Actor& actor) const {
    auto component = CreateDetached(typeName);
    if (!component)
        return nullptr;
    const std::type_index componentType(typeid(*component));
    return actor.AddComponentObject(componentType, std::move(component), true);
}

std::unique_ptr<Component> ComponentRegistry::CreateDetached(const std::string& typeName) const {
    return TypeRegistry::Get().Create(typeName);
}

bool ComponentRegistry::IsRegistered(const std::string& typeName) const {
    return TypeRegistry::Get().Find(typeName) != nullptr;
}

std::vector<std::string> ComponentRegistry::GetRegisteredTypes() const {
    return TypeRegistry::Get().GetRegisteredTypes();
}
