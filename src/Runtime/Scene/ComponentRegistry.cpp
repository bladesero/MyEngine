#include "Scene/ComponentRegistry.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scripting/ScriptComponent.h"

#include <algorithm>

ComponentRegistry& ComponentRegistry::Get()
{
    static ComponentRegistry registry;
    return registry;
}

ComponentRegistry::ComponentRegistry()
{
    Register("MeshRenderer", [](Actor& actor) {
        return actor.AddComponent<MeshRendererComponent>();
    });
    Register("SkinnedMeshRenderer", [](Actor& actor) {
        return actor.AddComponent<SkinnedMeshRendererComponent>();
    });
    Register("Script", [](Actor& actor) {
        return actor.AddComponent<ScriptComponent>();
    });
    Register("RigidBody", [](Actor& actor) {
        return actor.AddComponent<RigidBodyComponent>();
    });
    Register("BoxCollider", [](Actor& actor) {
        return actor.AddComponent<BoxColliderComponent>();
    });
    Register("SphereCollider", [](Actor& actor) {
        return actor.AddComponent<SphereColliderComponent>();
    });
    Register("CapsuleCollider", [](Actor& actor) {
        return actor.AddComponent<CapsuleColliderComponent>();
    });
    Register("CharacterController", [](Actor& actor) {
        return actor.AddComponent<CharacterControllerComponent>();
    });
    Register("Light", [](Actor& actor) {
        return actor.AddComponent<LightComponent>();
    });
    Register("PostProcess", [](Actor& actor) {
        return actor.AddComponent<PostProcessComponent>();
    });
}

bool ComponentRegistry::Register(const std::string& typeName, Factory factory)
{
    if (typeName.empty() || !factory) return false;
    return m_Factories.emplace(typeName, std::move(factory)).second;
}

Component* ComponentRegistry::Create(const std::string& typeName, Actor& actor) const
{
    const auto it = m_Factories.find(typeName);
    return it != m_Factories.end() ? it->second(actor) : nullptr;
}

bool ComponentRegistry::IsRegistered(const std::string& typeName) const
{
    return m_Factories.count(typeName) != 0;
}

std::vector<std::string> ComponentRegistry::GetRegisteredTypes() const
{
    std::vector<std::string> result;
    result.reserve(m_Factories.size());
    for (const auto& entry : m_Factories) result.push_back(entry.first);
    std::sort(result.begin(), result.end());
    return result;
}
