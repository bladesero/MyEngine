#include "Scene/ComponentRegistry.h"

#include "Audio/AudioSourceComponent.h"
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
    Register("MeshRenderer", [] { return std::make_unique<MeshRendererComponent>(); });
    Register("SkinnedMeshRenderer", [] { return std::make_unique<SkinnedMeshRendererComponent>(); });
    Register("Script", [] { return std::make_unique<ScriptComponent>(); });
    Register("RigidBody", [] { return std::make_unique<RigidBodyComponent>(); });
    Register("BoxCollider", [] { return std::make_unique<BoxColliderComponent>(); });
    Register("SphereCollider", [] { return std::make_unique<SphereColliderComponent>(); });
    Register("CapsuleCollider", [] { return std::make_unique<CapsuleColliderComponent>(); });
    Register("CharacterController", [] { return std::make_unique<CharacterControllerComponent>(); });
    Register("Light", [] { return std::make_unique<LightComponent>(); });
    Register("PostProcess", [] { return std::make_unique<PostProcessComponent>(); });
    Register("AudioSource", [] { return std::make_unique<AudioSourceComponent>(); });
}

bool ComponentRegistry::Register(const std::string& typeName, Factory factory)
{
    if (typeName.empty() || !factory) return false;
    return m_Factories.emplace(typeName, std::move(factory)).second;
}

Component* ComponentRegistry::Create(const std::string& typeName, Actor& actor) const
{
    auto component = CreateDetached(typeName);
    if (!component) return nullptr;
    const std::type_index componentType(typeid(*component));
    return actor.AddComponentObject(componentType, std::move(component), true);
}

std::unique_ptr<Component> ComponentRegistry::CreateDetached(const std::string& typeName) const
{
    const auto it = m_Factories.find(typeName);
    return it != m_Factories.end() ? it->second() : nullptr;
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
