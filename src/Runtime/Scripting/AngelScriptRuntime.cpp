#include "Scripting/AngelScriptRuntime.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSourceComponent.h"
#include "Audio/AudioListenerComponent.h"
#include "Camera/CameraComponent.h"
#include "Core/RuntimeFileSystem.h"
#include "Camera/ThirdPersonCameraComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/ParticleSystemComponent.h"
#include "Input/Input.h"
#include "Core/Logger.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Animation/AnimatorComponent.h"
#include "Gameplay/GameplayComponents.h"
#include "Gameplay/EnemyAIComponent.h"
#include "Game/SceneManager.h"
#include "Project/SaveGame.h"
#include "Navigation/NavAgentComponent.h"
#include "Scene/Actor.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/TypeRegistry.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"
#include "Scripting/ScriptBindingContext.h"
#include "Scripting/ScriptProfiler.h"
#include "UI/Core/UIComponents.h"
#include "UI/Core/UISystem.h"
#include "UI/UIEventBridge.h"

#include <angelscript.h>
#include <scriptstdstring.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <new>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
thread_local ScriptComponent* g_ActiveComponent = nullptr;

class ActiveScriptComponentScope {
public:
    explicit ActiveScriptComponentScope(ScriptComponent& component)
        : m_Previous(g_ActiveComponent)
    {
        g_ActiveComponent = &component;
    }

    ~ActiveScriptComponentScope()
    {
        g_ActiveComponent = m_Previous;
    }

private:
    ScriptComponent* m_Previous = nullptr;
};

ScriptBindingContext& Bindings()
{
    return ScriptBindingContext::Get();
}

UIEventBridge* UIEventBridgeBinding()
{
    return Bindings().GetUIEventBridge();
}

UISystem* UISystemBinding()
{
    return Bindings().GetUISystem();
}

ScriptComponent* ActiveComponent()
{
    return g_ActiveComponent;
}

Actor* ActiveActor()
{
    ScriptComponent* component = ActiveComponent();
    return component ? component->GetOwner() : nullptr;
}

Scene* ActiveScene()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetScene() : nullptr;
}

ActorHandle ActiveActorHandle()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetHandle() : ActorHandle{};
}

Actor* ResolveActor(ActorHandle handle)
{
    Scene* scene = ActiveScene();
    return scene ? scene->TryGetActor(handle) : nullptr;
}

Actor* ResolveActorOrSelf(ActorHandle handle)
{
    return handle.IsValid() ? ResolveActor(handle) : ActiveActor();
}

std::vector<uint64_t> ToUInt64Values(const std::vector<ActorHandle>& handles)
{
    std::vector<uint64_t> values;
    values.reserve(handles.size());
    for (const ActorHandle& handle : handles) values.push_back(handle.ToUInt64());
    return values;
}

class ScriptUInt64Array {
public:
    explicit ScriptUInt64Array(std::vector<uint64_t> values = {})
        : m_Values(std::move(values))
    {
    }

    void AddRef() { ++m_RefCount; }
    void Release()
    {
        if (--m_RefCount == 0) delete this;
    }
    uint32_t Length() const { return static_cast<uint32_t>(m_Values.size()); }
    uint64_t At(uint32_t index) const
    {
        return index < m_Values.size() ? m_Values[index] : 0;
    }

private:
    std::vector<uint64_t> m_Values;
    int m_RefCount = 1;
};

void UInt64ArrayAddRef(ScriptUInt64Array* array) { if (array) array->AddRef(); }
void UInt64ArrayRelease(ScriptUInt64Array* array) { if (array) array->Release(); }
uint32_t UInt64ArrayLength(const ScriptUInt64Array* array) { return array ? array->Length() : 0; }
uint64_t UInt64ArrayAt(uint32_t index, const ScriptUInt64Array* array)
{
    return array ? array->At(index) : 0;
}
ActorHandle ActorHandleArrayAt(uint32_t index, const ScriptUInt64Array* array)
{
    return ActorHandle::FromUInt64(array ? array->At(index) : 0);
}

std::string TrimCopy(const std::string& value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool TryParseIncludeDirective(const std::string& line, std::string& includePath)
{
    const std::string trimmed = TrimCopy(line);
    const bool isHashInclude = trimmed.rfind("#include", 0) == 0;
    const bool isImport = trimmed.rfind("import", 0) == 0;
    if (!isHashInclude && !isImport) return false;
    const size_t firstQuote = trimmed.find('"');
    const size_t secondQuote = firstQuote == std::string::npos ? std::string::npos
        : trimmed.find('"', firstQuote + 1);
    if (firstQuote == std::string::npos || secondQuote == std::string::npos) return false;
    includePath = trimmed.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    return true;
}

bool IsSafeScriptIncludePath(const std::string& path)
{
    if (path.empty()) return false;
    const std::filesystem::path input(path);
    if (input.is_absolute()) return false;
    for (const auto& part : input) {
        if (part == "..") return false;
    }
    return true;
}

bool StartsWithGenericPath(const std::string& path, const std::string& prefix)
{
    return path.rfind(prefix, 0) == 0;
}

std::filesystem::path ResolveScriptIncludePath(const std::string& includePath,
                                               const std::filesystem::path& baseDirectory)
{
    if (!IsSafeScriptIncludePath(includePath)) return {};
    const std::filesystem::path input(includePath);
    const std::string generic = input.generic_string();
    std::vector<std::filesystem::path> candidates;
    if (StartsWithGenericPath(generic, "Content/")) {
        candidates.emplace_back(AssetManager::Get().ResolvePath(generic));
    }
    if (StartsWithGenericPath(generic, "EngineContent/")) {
        candidates.emplace_back(std::filesystem::current_path() / input);
        candidates.emplace_back(std::filesystem::path("Content/Engine") /
            std::filesystem::path(generic.substr(std::string("EngineContent/").size())));
    }
    if (!baseDirectory.empty()) {
        candidates.emplace_back(baseDirectory / input);
    }
    const std::filesystem::path projectRoot = AssetManager::Get().GetProjectRoot();
    if (!projectRoot.empty()) {
        candidates.emplace_back(projectRoot / input);
        candidates.emplace_back(projectRoot / "Content" / "Scripts" / input);
    }
    candidates.emplace_back(std::filesystem::current_path() / input);

    for (const auto& candidate : candidates) {
        if (RuntimeFileSystem::Get().Exists(candidate.generic_string())) {
            return candidate.lexically_normal();
        }
        std::error_code error;
        const auto normalized = std::filesystem::absolute(candidate, error).lexically_normal();
        if (error) continue;
        if (RuntimeFileSystem::Get().Exists(normalized.string())) return normalized;
    }
    return {};
}

bool PreprocessScriptSourceRecursive(const std::string& source,
                                     const std::string& chunkName,
                                     const std::filesystem::path& baseDirectory,
                                     std::string& outSource,
                                     std::vector<std::string>* dependencies,
                                     std::unordered_set<std::string>& includeStack,
                                     std::string& error,
                                     int depth)
{
    if (depth > 32) {
        error = "script include depth exceeded at " + chunkName;
        return false;
    }
    std::istringstream input(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string includePath;
        if (!TryParseIncludeDirective(line, includePath)) {
            outSource += line;
            outSource += '\n';
            continue;
        }
        const std::filesystem::path resolved = ResolveScriptIncludePath(includePath, baseDirectory);
        if (resolved.empty()) {
            error = chunkName + ":" + std::to_string(lineNumber) +
                ": failed to resolve script include '" + includePath + "'";
            return false;
        }
        const std::string key = resolved.string();
        if (includeStack.count(key)) {
            error = chunkName + ":" + std::to_string(lineNumber) +
                ": cyclic script include '" + includePath + "'";
            return false;
        }
        includeStack.insert(key);
        std::string includeSource;
        if (!RuntimeFileSystem::Get().ReadText(key, includeSource)) {
            error = "failed to open script include: " + key;
            includeStack.erase(key);
            return false;
        }
        if (dependencies) dependencies->push_back(key);
        outSource += "\n// begin include: ";
        outSource += includePath;
        outSource += "\n";
        if (!PreprocessScriptSourceRecursive(includeSource, key, resolved.parent_path(),
                                             outSource, dependencies, includeStack, error, depth + 1)) {
            includeStack.erase(key);
            return false;
        }
        outSource += "// end include: ";
        outSource += includePath;
        outSource += "\n";
        includeStack.erase(key);
    }
    return true;
}

Vec3 ReadActorPosition()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetTransform().position : Vec3{};
}

Vec3 ReadActorWorldPosition()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetWorldPosition() : Vec3{};
}

Vec3 ReadActorRotation()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetTransform().rotation : Vec3{};
}

Vec3 ReadParentRotation()
{
    Actor* actor = ActiveActor();
    Actor* parent = actor ? actor->GetParent() : nullptr;
    return parent ? parent->GetTransform().rotation : Vec3{};
}

Vec3 ActorGetForward()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetWorldMatrix().TransformDir(Vec3::Forward()).Normalized()
                 : Vec3::Forward();
}

Vec3 ActorGetRight()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetWorldMatrix().TransformDir(Vec3::Right()).Normalized()
                 : Vec3::Right();
}

Vec3 ParentGetForward()
{
    Actor* actor = ActiveActor();
    Actor* parent = actor ? actor->GetParent() : nullptr;
    return parent ? parent->GetWorldMatrix().TransformDir(Vec3::Forward()).Normalized()
                  : Vec3::Forward();
}

Vec3 ParentGetRight()
{
    Actor* actor = ActiveActor();
    Actor* parent = actor ? actor->GetParent() : nullptr;
    return parent ? parent->GetWorldMatrix().TransformDir(Vec3::Right()).Normalized()
                  : Vec3::Right();
}

std::string ActorGetName()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetName() : std::string{};
}

void ActorSetPosition(const Vec3& value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().position = value;
}

void ActorSetRotation(const Vec3& value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().rotation = value;
}

void ActorSetParentRotation(const Vec3& value)
{
    Actor* actor = ActiveActor();
    Actor* parent = actor ? actor->GetParent() : nullptr;
    if (parent) parent->GetTransform().rotation = value;
}

void ActorTranslate(const Vec3& value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().position += value;
}

void ActorRotate(const Vec3& value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().rotation += value;
}

ActorHandle SceneGetSelf() { return ActiveActorHandle(); }
bool SceneIsValid(ActorHandle handle) { return ResolveActor(handle) != nullptr; }

ActorHandle SceneFindByName(const std::string& name)
{
    Scene* scene = ActiveScene();
    Actor* actor = scene ? scene->FindByName(name) : nullptr;
    return actor ? actor->GetHandle() : ActorHandle{};
}

ActorHandle SceneFindByTag(const std::string& tag)
{
    if (tag.empty()) return {};
    Scene* scene = ActiveScene();
    if (!scene) return {};
    ActorHandle result;
    scene->ForEach([&](Actor& actor) {
        if (!result && actor.HasTag(tag)) result = actor.GetHandle();
    });
    return result;
}

ScriptUInt64Array* SceneFindAllByName(const std::string& name)
{
    std::vector<uint64_t> values;
    if (Scene* scene = ActiveScene()) {
        scene->ForEach([&](Actor& actor) {
            if (actor.GetName() == name) values.push_back(actor.GetHandle().ToUInt64());
        });
    }
    return new ScriptUInt64Array(std::move(values));
}

ScriptUInt64Array* SceneFindAllByTag(const std::string& tag)
{
    std::vector<uint64_t> values;
    if (!tag.empty()) {
        if (Scene* scene = ActiveScene()) {
            scene->ForEach([&](Actor& actor) {
                if (actor.HasTag(tag)) values.push_back(actor.GetHandle().ToUInt64());
            });
        }
    }
    return new ScriptUInt64Array(std::move(values));
}

ColliderComponent* FirstCollider(Actor& actor);

ScriptUInt64Array* SceneFindAllInLayer(uint32_t layer)
{
    std::vector<uint64_t> values;
    if (Scene* scene = ActiveScene()) {
        scene->ForEach([&](Actor& actor) {
            ColliderComponent* collider = FirstCollider(actor);
            if (actor.GetLayer() == layer || (collider && collider->GetLayer() == layer)) {
                values.push_back(actor.GetHandle().ToUInt64());
            }
        });
    }
    return new ScriptUInt64Array(std::move(values));
}

std::string SceneGetName(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetName() : std::string{};
}

void SceneSetName(ActorHandle handle, const std::string& name)
{
    try {
        if (Actor* actor = ResolveActor(handle)) actor->SetName(name);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene set name failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene set name failed with an unknown error");
    }
}

ActorHandle SceneGetParent(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    Actor* parent = actor ? actor->GetParent() : nullptr;
    return parent ? parent->GetHandle() : ActorHandle{};
}

ScriptUInt64Array* SceneGetChildren(ActorHandle handle)
{
    std::vector<uint64_t> values;
    if (Actor* actor = ResolveActor(handle)) {
        for (Actor* child : actor->GetChildren()) {
            if (child) values.push_back(child->GetHandle().ToUInt64());
        }
    }
    return new ScriptUInt64Array(std::move(values));
}

ScriptUInt64Array* SceneFindAllWithComponent(const std::string& type)
{
    std::vector<uint64_t> values;
    if (Scene* scene = ActiveScene()) {
        scene->ForEach([&](Actor& actor) {
            if (actor.HasComponentType(type)) values.push_back(actor.GetHandle().ToUInt64());
        });
    }
    return new ScriptUInt64Array(std::move(values));
}

ColliderComponent* FirstCollider(Actor& actor)
{
    ColliderComponent* collider = nullptr;
    actor.ForEachComponent([&](Component& component) {
        if (!collider) collider = dynamic_cast<ColliderComponent*>(&component);
    });
    return collider;
}

void ForEachCollider(Actor& actor, const std::function<void(ColliderComponent&)>& fn)
{
    actor.ForEachComponent([&](Component& component) {
        if (auto* collider = dynamic_cast<ColliderComponent*>(&component)) fn(*collider);
    });
}

ScriptUInt64Array* SceneFindInRadius(const Vec3& center, float radius, uint32_t mask)
{
    std::vector<uint64_t> values;
    Scene* scene = ActiveScene();
    if (!scene || radius < 0.0f) return new ScriptUInt64Array(std::move(values));
    const float radiusSq = radius * radius;
    scene->ForEach([&](Actor& actor) {
        const Vec3 delta = actor.GetWorldPosition() - center;
        if (delta.Dot(delta) > radiusSq) return;
        ColliderComponent* collider = FirstCollider(actor);
        const uint32_t layer = collider ? collider->GetLayer() : 1u;
        if ((layer & mask) == 0) return;
        values.push_back(actor.GetHandle().ToUInt64());
    });
    return new ScriptUInt64Array(std::move(values));
}

bool SceneGetActive(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor && actor->IsActiveSelf();
}

uint32_t SceneGetLayer(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    if (!actor) return 0u;
    if (actor->GetLayer() != 0u) return actor->GetLayer();
    ColliderComponent* collider = FirstCollider(*actor);
    return collider ? collider->GetLayer() : 0u;
}

ActorHandle SceneCreateActor(const std::string& name)
{
    try {
        Scene* scene = ActiveScene();
        if (!scene) return {};
        ActorCreateDesc desc;
        desc.name = name.empty() ? "Actor" : name;
        return scene->QueueCreateActor(desc);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene create actor failed: ", e.what());
        return {};
    } catch (...) {
        Logger::Warn("[Scripting] scene create actor failed with an unknown error");
        return {};
    }
}

void SceneDestroyActor(ActorHandle handle)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueDestroyActor(handle);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene destroy actor failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene destroy actor failed with an unknown error");
    }
}

void SceneSetActive(ActorHandle handle, bool active)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueSetActive(handle, active);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene set active failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene set active failed with an unknown error");
    }
}

void SceneSetLayer(ActorHandle handle, uint32_t layer)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueSetLayer(handle, layer);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene set layer failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene set layer failed with an unknown error");
    }
}

void SceneSetTag(ActorHandle handle, const std::string& tag)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueSetTag(handle, tag);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene set tag failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene set tag failed with an unknown error");
    }
}

void SceneSetParent(ActorHandle child, ActorHandle parent)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueSetParent(child, parent);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene set parent failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene set parent failed with an unknown error");
    }
}

void SceneMoveActor(ActorHandle child, ActorHandle parent, ActorHandle nextSibling)
{
    try {
        if (Scene* scene = ActiveScene()) scene->QueueMoveActor(child, parent, nextSibling);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] scene move actor failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] scene move actor failed with an unknown error");
    }
}

ActorHandle SceneFindNearestWithComponent(const std::string& type, const Vec3& center, float maxDistance)
{
    if (type.empty() || maxDistance < 0.0f) return {};
    Scene* scene = ActiveScene();
    if (!scene) return {};
    ActorHandle best;
    float bestDistanceSq = maxDistance * maxDistance;
    scene->ForEach([&](Actor& actor) {
        if (!actor.HasComponentType(type)) return;
        const Vec3 delta = actor.GetWorldPosition() - center;
        const float distanceSq = delta.Dot(delta);
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            best = actor.GetHandle();
        }
    });
    return best;
}

float SceneGetDistance(ActorHandle a, ActorHandle b)
{
    Actor* lhs = ResolveActor(a);
    Actor* rhs = ResolveActor(b);
    if (!lhs || !rhs) return 0.0f;
    const Vec3 delta = lhs->GetWorldPosition() - rhs->GetWorldPosition();
    return std::sqrt(delta.Dot(delta));
}

bool IsProjectRelativeContentPath(const std::string& path)
{
    try {
        const std::filesystem::path input(path);
        if (path.empty() || input.is_absolute()) return false;
        const std::string normalized = input.lexically_normal().generic_string();
        if (normalized == "Content" || normalized.rfind("Content/", 0) != 0) return false;
        for (const auto& part : input) {
            if (part == "..") return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

ActorHandle SceneInstantiatePrefab(const std::string& path, const Vec3& position, const Vec3& rotation)
{
    try {
        Scene* scene = ActiveScene();
        if (!scene || !IsProjectRelativeContentPath(path)) return {};
        PrefabInstantiateOptions options;
        Transform transform;
        transform.position = position;
        transform.rotation = rotation;
        options.rootTransform = transform;
        std::string error;
        const ActorHandle handle = PrefabSystem::QueueInstantiate(*scene, path, options, &error);
        if (!handle && !error.empty()) Logger::Warn("[Scripting] prefab instantiate failed: ", error);
        return handle;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] prefab instantiate failed: ", e.what());
        return {};
    } catch (...) {
        Logger::Warn("[Scripting] prefab instantiate failed with an unknown error");
        return {};
    }
}

Vec3 ActorGetPositionHandle(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetTransform().position : Vec3{};
}

Vec3 ActorGetWorldPositionHandle(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetWorldPosition() : Vec3{};
}

Vec3 ActorGetRotationHandle(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetTransform().rotation : Vec3{};
}

void ActorSetPositionHandle(ActorHandle handle, const Vec3& value)
{
    if (Actor* actor = ResolveActor(handle)) actor->GetTransform().position = value;
}

void ActorSetRotationHandle(ActorHandle handle, const Vec3& value)
{
    if (Actor* actor = ResolveActor(handle)) actor->GetTransform().rotation = value;
}

void ActorTranslateHandle(ActorHandle handle, const Vec3& value)
{
    if (Actor* actor = ResolveActor(handle)) actor->GetTransform().position += value;
}

void ActorRotateHandle(ActorHandle handle, const Vec3& value)
{
    if (Actor* actor = ResolveActor(handle)) actor->GetTransform().rotation += value;
}

Vec3 ActorGetScaleHandle(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetTransform().scale : Vec3::One();
}

void ActorSetScaleHandle(ActorHandle handle, const Vec3& value)
{
    if (Actor* actor = ResolveActor(handle)) actor->GetTransform().scale = value;
}

Vec3 TransformGetPosition() { return ReadActorPosition(); }
Vec3 TransformGetRotation() { return ReadActorRotation(); }
Vec3 TransformGetScale()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetTransform().scale : Vec3::One();
}
Vec3 TransformGetPositionHandle(ActorHandle handle) { return ActorGetPositionHandle(handle); }
Vec3 TransformGetRotationHandle(ActorHandle handle) { return ActorGetRotationHandle(handle); }
Vec3 TransformGetScaleHandle(ActorHandle handle) { return ActorGetScaleHandle(handle); }
void TransformSetPosition(const Vec3& value) { ActorSetPosition(value); }
void TransformSetRotation(const Vec3& value) { ActorSetRotation(value); }
void TransformSetScale(const Vec3& value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().scale = value;
}
void TransformSetPositionHandle(ActorHandle handle, const Vec3& value) { ActorSetPositionHandle(handle, value); }
void TransformSetRotationHandle(ActorHandle handle, const Vec3& value) { ActorSetRotationHandle(handle, value); }
void TransformSetScaleHandle(ActorHandle handle, const Vec3& value) { ActorSetScaleHandle(handle, value); }

std::string TagsGet()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetTag() : std::string{};
}

std::string TagsGetHandle(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    return actor ? actor->GetTag() : std::string{};
}

void TagsSet(const std::string& tag)
{
    if (Actor* actor = ActiveActor()) SceneSetTag(actor->GetHandle(), tag);
}

void TagsSetHandle(ActorHandle handle, const std::string& tag)
{
    SceneSetTag(handle, tag);
}

bool TagsHas(const std::string& tag)
{
    Actor* actor = ActiveActor();
    return actor && actor->HasTag(tag);
}

bool TagsHasHandle(ActorHandle handle, const std::string& tag)
{
    Actor* actor = ResolveActor(handle);
    return actor && actor->HasTag(tag);
}

bool PrefabInstanceIsInstance(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor && actor->IsPrefabInstance();
}

bool PrefabInstanceIsRoot(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor && actor->IsPrefabRoot();
}

std::string PrefabInstanceGetAssetPath(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetPrefabAssetPath() : std::string{};
}

ActorHandle PrefabInstanceGetRoot(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetPrefabInstanceRoot() : ActorHandle{};
}

bool AudioListenerSetEnabled(ActorHandle handle, bool enabled) { Actor* actor=ResolveActorOrSelf(handle);auto* listener=actor?actor->GetComponent<AudioListenerComponent>():nullptr;if(!listener)return false;listener->SetEnabled(enabled);return true; }
bool AudioListenerIsEnabled(ActorHandle handle) { Actor* actor=ResolveActorOrSelf(handle);auto* listener=actor?actor->GetComponent<AudioListenerComponent>():nullptr;return listener&&listener->IsEnabled(); }
bool ParticlePlay(ActorHandle handle) { Actor* actor=ResolveActorOrSelf(handle);auto* particles=actor?actor->GetComponent<ParticleSystemComponent>():nullptr;return particles&&particles->Play(); }
void ParticleStop(ActorHandle handle) { Actor* actor=ResolveActorOrSelf(handle);if(auto* particles=actor?actor->GetComponent<ParticleSystemComponent>():nullptr)particles->Stop(); }
bool ParticleIsPlaying(ActorHandle handle) { Actor* actor=ResolveActorOrSelf(handle);auto* particles=actor?actor->GetComponent<ParticleSystemComponent>():nullptr;return particles&&particles->IsPlaying(); }
void ParticleEmit(ActorHandle handle,uint32_t count){Actor* actor=ResolveActorOrSelf(handle);if(auto* particles=actor?actor->GetComponent<ParticleSystemComponent>():nullptr)particles->Emit(count);}

RigidBodyComponent* ActiveBody()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetComponent<RigidBodyComponent>() : nullptr;
}

RigidBodyComponent* ResolveBody(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<RigidBodyComponent>() : nullptr;
}

void BodySetVelocity(const Vec3& value) { if (auto* body = ActiveBody()) body->SetVelocity(value); }
void BodyAddForce(const Vec3& value) { if (auto* body = ActiveBody()) body->AddForce(value); }
void BodySetAngularVelocity(const Vec3& value) { if (auto* body = ActiveBody()) body->SetAngularVelocity(value); }
void BodyAddTorque(const Vec3& value) { if (auto* body = ActiveBody()) body->AddTorque(value); }
void BodyAddImpulse(const Vec3& value) { if (auto* body = ActiveBody()) body->AddImpulse(value); }
void BodyAddAngularImpulse(const Vec3& value) { if (auto* body = ActiveBody()) body->AddAngularImpulse(value); }
void BodyTeleport(const Vec3& position, const Vec3& rotation) { if (auto* body = ActiveBody()) body->Teleport(position, rotation); }
void BodySetKinematicTarget(const Vec3& position, const Vec3& rotation) { if (auto* body = ActiveBody()) body->SetKinematicTarget(position, rotation); }
Vec3 BodyGetVelocity(ActorHandle handle) { if (auto* body = ResolveBody(handle)) return body->GetVelocity(); return Vec3{}; }
void BodySetVelocityHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->SetVelocity(value); }
void BodyAddForceHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->AddForce(value); }
void BodySetAngularVelocityHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->SetAngularVelocity(value); }
void BodyAddTorqueHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->AddTorque(value); }
void BodyAddImpulseHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->AddImpulse(value); }
void BodyAddAngularImpulseHandle(ActorHandle handle, const Vec3& value) { if (auto* body = ResolveBody(handle)) body->AddAngularImpulse(value); }
void BodyTeleportHandle(ActorHandle handle, const Vec3& position, const Vec3& rotation) { if (auto* body = ResolveBody(handle)) body->Teleport(position, rotation); }
void BodySetKinematicTargetHandle(ActorHandle handle, const Vec3& position, const Vec3& rotation) { if (auto* body = ResolveBody(handle)) body->SetKinematicTarget(position, rotation); }
void BodySetUseGravity(ActorHandle handle, bool value) { if (auto* body = ResolveBody(handle)) body->SetUseGravity(value); }
bool BodyUsesGravity(ActorHandle handle) { if (auto* body = ResolveBody(handle)) return body->UsesGravity(); return false; }

CharacterControllerComponent* ActiveCharacterController()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetComponent<CharacterControllerComponent>() : nullptr;
}

CharacterControllerComponent* ResolveCharacterController(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<CharacterControllerComponent>() : nullptr;
}

void CharacterControllerMove(const Vec3& velocity)
{
    if (auto* controller = ActiveCharacterController()) controller->Move(velocity);
}

bool CharacterControllerIsGrounded()
{
    if (auto* controller = ActiveCharacterController()) return controller->IsGrounded();
    return false;
}

void CharacterControllerSetUseGravity(bool enabled)
{
    if (auto* controller = ActiveCharacterController()) controller->SetUseGravity(enabled);
}

void CharacterControllerMoveHandle(ActorHandle handle, const Vec3& velocity)
{
    if (auto* controller = ResolveCharacterController(handle)) controller->Move(velocity);
}

bool CharacterControllerIsGroundedHandle(ActorHandle handle)
{
    if (auto* controller = ResolveCharacterController(handle)) return controller->IsGrounded();
    return false;
}

void CharacterControllerSetUseGravityHandle(ActorHandle handle, bool enabled)
{
    if (auto* controller = ResolveCharacterController(handle)) controller->SetUseGravity(enabled);
}

bool CharacterControllerJump(float speed)
{
    if (auto* controller = ActiveCharacterController()) return controller->Jump(speed);
    return false;
}

bool CharacterControllerJumpHandle(ActorHandle handle, float speed)
{
    if (auto* controller = ResolveCharacterController(handle)) return controller->Jump(speed);
    return false;
}

Vec3 CharacterControllerGetActualVelocity(ActorHandle handle)
{
    if (auto* controller = ResolveCharacterController(handle)) return controller->GetActualVelocity();
    return {};
}

void CharacterControllerSetAirControl(ActorHandle handle, float value)
{
    if (auto* controller = ResolveCharacterController(handle)) controller->SetAirControl(value);
}

ColliderComponent* ResolveCollider(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? FirstCollider(*actor) : nullptr;
}

bool ColliderIsTrigger(ActorHandle handle) { if (auto* collider = ResolveCollider(handle)) return collider->IsTrigger(); return false; }
void ColliderSetTrigger(ActorHandle handle, bool value) { if (auto* collider = ResolveCollider(handle)) collider->SetTrigger(value); }
uint32_t ColliderGetLayer(ActorHandle handle) { if (auto* collider = ResolveCollider(handle)) return collider->GetLayer(); return 0u; }
void ColliderSetLayer(ActorHandle handle, uint32_t value) { if (Actor* actor = ResolveActorOrSelf(handle)) ForEachCollider(*actor, [&](ColliderComponent& collider) { collider.SetLayer(value); }); }
uint32_t ColliderGetLayerMask(ActorHandle handle) { if (auto* collider = ResolveCollider(handle)) return collider->GetLayerMask(); return 0u; }
void ColliderSetLayerMask(ActorHandle handle, uint32_t value) { if (Actor* actor = ResolveActorOrSelf(handle)) ForEachCollider(*actor, [&](ColliderComponent& collider) { collider.SetLayerMask(value); }); }

bool ComponentsHas(ActorHandle handle, const std::string& type)
{
    Actor* actor = ResolveActor(handle);
    return actor && actor->HasComponentType(type);
}

bool ComponentsAdd(ActorHandle handle, const std::string& type)
{
    try {
        Scene* scene = ActiveScene();
        if (!scene || !ComponentRegistry::Get().IsRegistered(type)) return false;
        scene->QueueAddComponent(handle, type);
        return true;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] component add failed: ", e.what());
        return false;
    } catch (...) {
        Logger::Warn("[Scripting] component add failed with an unknown error");
        return false;
    }
}

bool ComponentsRemove(ActorHandle handle, const std::string& type)
{
    try {
        Scene* scene = ActiveScene();
        if (!scene || type.empty()) return false;
        scene->QueueRemoveComponent({handle, type});
        return true;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] component remove failed: ", e.what());
        return false;
    } catch (...) {
        Logger::Warn("[Scripting] component remove failed with an unknown error");
        return false;
    }
}

std::string ComponentsGetJson(ActorHandle handle, const std::string& type)
{
    Actor* actor = ResolveActor(handle);
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    if (!component) return "{}";
    nlohmann::json data = nlohmann::json::object();
    component->Serialize(data);
    return data.dump();
}

bool ComponentsSetJson(ActorHandle handle, const std::string& type, const std::string& json)
{
    Actor* actor = ResolveActor(handle);
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    if (!component) return false;
    try {
        const nlohmann::json data = nlohmann::json::parse(json.empty() ? "{}" : json);
        if (!data.is_object()) return false;
        component->Deserialize(data);
        return true;
    } catch (...) {
        return false;
    }
}

AudioSourceComponent* ResolveAudioSource(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<AudioSourceComponent>() : nullptr;
}

bool AudioSourcePlay() { if (auto* source = ResolveAudioSource({})) return source->Play(); return false; }
bool AudioSourcePlayHandle(ActorHandle handle) { if (auto* source = ResolveAudioSource(handle)) return source->Play(); return false; }
void AudioSourceStop() { if (auto* source = ResolveAudioSource({})) source->Stop(); }
void AudioSourceStopHandle(ActorHandle handle) { if (auto* source = ResolveAudioSource(handle)) source->Stop(); }
bool AudioSourceIsPlaying() { if (auto* source = ResolveAudioSource({})) return source->IsPlaying(); return false; }
bool AudioSourceIsPlayingHandle(ActorHandle handle) { if (auto* source = ResolveAudioSource(handle)) return source->IsPlaying(); return false; }
void AudioSourceSetClipPath(const std::string& path) { if (auto* source = ResolveAudioSource({})) source->SetClipPath(path); }
void AudioSourceSetClipPathHandle(ActorHandle handle, const std::string& path) { if (auto* source = ResolveAudioSource(handle)) source->SetClipPath(path); }
void AudioSourceSetVolume(float value) { if (auto* source = ResolveAudioSource({})) source->SetVolume(value); }
void AudioSourceSetVolumeHandle(ActorHandle handle, float value) { if (auto* source = ResolveAudioSource(handle)) source->SetVolume(value); }
void AudioSourceSetPitch(float value) { if (auto* source = ResolveAudioSource({})) source->SetPitch(value); }
void AudioSourceSetPitchHandle(ActorHandle handle, float value) { if (auto* source = ResolveAudioSource(handle)) source->SetPitch(value); }
void AudioSourceSetLoop(bool value) { if (auto* source = ResolveAudioSource({})) source->SetLoop(value); }
void AudioSourceSetLoopHandle(ActorHandle handle, bool value) { if (auto* source = ResolveAudioSource(handle)) source->SetLoop(value); }

CameraComponent* ResolveCamera(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<CameraComponent>() : nullptr;
}

void CameraSetMain(ActorHandle handle, bool value) { if (auto* camera = ResolveCamera(handle)) camera->SetMainCamera(value); }
bool CameraIsMain(ActorHandle handle) { if (auto* camera = ResolveCamera(handle)) return camera->IsMainCamera(); return false; }
void CameraSetFovY(ActorHandle handle, float value) { if (auto* camera = ResolveCamera(handle)) camera->SetFovYDegrees(value); }
float CameraGetFovY(ActorHandle handle) { if (auto* camera = ResolveCamera(handle)) return camera->GetFovYDegrees(); return 0.0f; }

LightComponent* ResolveLight(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<LightComponent>() : nullptr;
}

void LightSetIntensity(ActorHandle handle, float value) { if (auto* light = ResolveLight(handle)) light->SetIntensity(value); }
float LightGetIntensity(ActorHandle handle) { if (auto* light = ResolveLight(handle)) return light->GetIntensity(); return 0.0f; }
void LightSetColor(ActorHandle handle, const Vec3& value) { if (auto* light = ResolveLight(handle)) light->SetColor(value); }
Vec3 LightGetColor(ActorHandle handle) { if (auto* light = ResolveLight(handle)) return light->GetColor(); return Vec3::One(); }

MeshRendererComponent* ResolveMeshRenderer(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<MeshRendererComponent>() : nullptr;
}

std::string MeshRendererGetMaterialPath(ActorHandle handle, int slot)
{
    if (auto* renderer = ResolveMeshRenderer(handle)) {
        MaterialHandle material = renderer->GetMaterialForSlot(slot);
        return material ? AssetManager::Get().MakeProjectRelativePath(material->GetPath()) : std::string{};
    }
    return {};
}

bool MeshRendererSetMaterialPath(ActorHandle handle, int slot, const std::string& path)
{
    if (slot < 0) return false;
    if (auto* renderer = ResolveMeshRenderer(handle)) {
        renderer->SetMaterialSlot(static_cast<size_t>(slot), AssetManager::Get().ResolveMaterialReference(path));
        return renderer->GetMaterialForSlot(slot).IsValid();
    }
    return false;
}

SkinnedMeshRendererComponent* ResolveSkinnedMeshRenderer(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<SkinnedMeshRendererComponent>() : nullptr;
}

AnimatorComponent* ResolveAnimator(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<AnimatorComponent>() : nullptr;
}

bool AnimatorPlay(ActorHandle handle, const std::string& state, float transition)
{
    if (auto* animator = ResolveAnimator(handle)) return animator->Play(state, transition);
    return false;
}

void AnimatorSetFloat(ActorHandle handle, const std::string& name, float value)
{
    if (auto* animator = ResolveAnimator(handle)) animator->SetFloat(name, value);
}

float AnimatorGetFloat(ActorHandle handle, const std::string& name)
{
    if (auto* animator = ResolveAnimator(handle)) return animator->GetFloat(name);
    return 0.0f;
}

void AnimatorSetBool(ActorHandle handle, const std::string& name, bool value)
{
    if (auto* animator = ResolveAnimator(handle)) animator->SetBool(name, value);
}

bool AnimatorGetBool(ActorHandle handle, const std::string& name)
{
    if (auto* animator = ResolveAnimator(handle)) return animator->GetBool(name);
    return false;
}

void AnimatorSetTrigger(ActorHandle handle, const std::string& name)
{
    if (auto* animator = ResolveAnimator(handle)) animator->SetTrigger(name);
}

std::string AnimatorGetCurrentState(ActorHandle handle)
{
    if (auto* animator = ResolveAnimator(handle)) return animator->GetCurrentState();
    return {};
}

float AnimatorGetNormalizedTime(ActorHandle handle)
{
    if (auto* animator = ResolveAnimator(handle)) return animator->GetNormalizedTime();
    return 0.0f;
}

ThirdPersonCameraComponent* ResolveThirdPersonCamera(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<ThirdPersonCameraComponent>() : nullptr;
}

bool ThirdPersonCameraSetTarget(ActorHandle camera, ActorHandle target)
{
    auto* controller = ResolveThirdPersonCamera(camera);
    if (!controller || !ResolveActor(target)) return false;
    controller->SetTarget(target);
    return true;
}

void ThirdPersonCameraAddOrbit(ActorHandle handle, float yaw, float pitch)
{
    if (auto* controller = ResolveThirdPersonCamera(handle)) controller->AddOrbit(yaw, pitch);
}

void ThirdPersonCameraSetDistance(ActorHandle handle, float distance)
{
    if (auto* controller = ResolveThirdPersonCamera(handle)) controller->SetDistance(distance);
}

HealthComponent* ResolveHealth(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    return actor ? actor->GetComponent<HealthComponent>() : nullptr;
}

bool CombatDamage(ActorHandle target, float amount, ActorHandle source)
{
    Actor* targetActor = ResolveActor(target);
    auto* health = targetActor ? targetActor->GetComponent<HealthComponent>() : nullptr;
    if (!health) return false;
    DamageEvent event{source, target, amount, targetActor->GetWorldPosition(), Vec3::Zero()};
    return health->ApplyDamage(event);
}

bool CombatHeal(ActorHandle target, float amount) { if (auto* health=ResolveHealth(target)) return health->Heal(amount); return false; }
float CombatGetHealth(ActorHandle target) { if (auto* health=ResolveHealth(target)) return health->GetHealth(); return 0.0f; }
float CombatGetMaxHealth(ActorHandle target) { if (auto* health=ResolveHealth(target)) return health->GetMaxHealth(); return 0.0f; }
bool CombatIsDead(ActorHandle target) { if (auto* health=ResolveHealth(target)) return health->IsDead(); return false; }
bool CombatBeginAttack(ActorHandle attacker, float damage) { Actor* actor=ResolveActorOrSelf(attacker);auto* hitbox=actor?actor->GetComponent<HitboxComponent>():nullptr;if(!hitbox)return false;hitbox->BeginAttack(damage);return true; }
void CombatEndAttack(ActorHandle attacker) { Actor* actor=ResolveActorOrSelf(attacker);if(auto* hitbox=actor?actor->GetComponent<HitboxComponent>():nullptr)hitbox->EndAttack(); }

InteractionComponent* ResolveInteraction(ActorHandle handle)
{
    Actor* actor=ResolveActor(handle); return actor?actor->GetComponent<InteractionComponent>():nullptr;
}

ActorHandle InteractionFindNearest(ActorHandle instigator, float maxDistance)
{
    Scene* scene=ActiveScene(); Actor* source=scene?scene->TryGetActor(instigator):nullptr;
    if(!source || maxDistance<0.0f)return {};
    ActorHandle best; float bestDistance=maxDistance;
    scene->ForEach([&](Actor& actor){auto* interaction=actor.GetComponent<InteractionComponent>();if(!interaction||!interaction->CanInteract())return;float distance=(actor.GetWorldPosition()-source->GetWorldPosition()).Length();if(distance<=bestDistance&&distance<=interaction->GetRange()){best=actor.GetHandle();bestDistance=distance;}});
    return best;
}

bool InteractionUse(ActorHandle target, ActorHandle instigator) { if(auto* interaction=ResolveInteraction(target))return interaction->Interact(instigator);return false; }
std::string InteractionGetPrompt(ActorHandle target) { if(auto* interaction=ResolveInteraction(target))return interaction->GetPrompt();return {}; }
ActorHandle InteractionFindNearestSelf(float maxDistance){return InteractionFindNearest(ActiveActorHandle(),maxDistance);}
std::string InteractionGetNearestName(float maxDistance){Scene* scene=ActiveScene();Actor* actor=scene?scene->TryGetActor(InteractionFindNearestSelf(maxDistance)):nullptr;return actor?actor->GetName():std::string{};}
std::string InteractionGetNearestPrompt(float maxDistance){return InteractionGetPrompt(InteractionFindNearestSelf(maxDistance));}
std::string InteractionUseNearest(float maxDistance){Scene* scene=ActiveScene();const ActorHandle target=InteractionFindNearestSelf(maxDistance);Actor* actor=scene?scene->TryGetActor(target):nullptr;auto* interaction=actor?actor->GetComponent<InteractionComponent>():nullptr;if(!actor||!interaction||!interaction->Interact(ActiveActorHandle()))return{};const std::string name=actor->GetName();if(interaction->GetDestroyOnUse())scene->QueueDestroyActor(target);return name;}

GameplayFeedbackComponent* ResolveFeedback(ActorHandle handle){Actor* actor=ResolveActorOrSelf(handle);return actor?actor->GetComponent<GameplayFeedbackComponent>():nullptr;}
bool FeedbackShake(ActorHandle handle,float amplitude,float duration){if(auto* feedback=ResolveFeedback(handle)){feedback->Shake(amplitude,duration);return true;}return false;}
bool FeedbackFlash(ActorHandle handle,float intensity,float duration){if(auto* feedback=ResolveFeedback(handle)){feedback->Flash(intensity,duration);return true;}return false;}
bool FeedbackSlowMotion(ActorHandle handle,float scale,float duration){if(auto* feedback=ResolveFeedback(handle)){feedback->SlowMotion(scale,duration);return true;}return false;}

NavAgentComponent* ResolveNavAgent(ActorHandle handle){Actor* actor=ResolveActorOrSelf(handle);return actor?actor->GetComponent<NavAgentComponent>():nullptr;}
bool NavigationSetDestination(ActorHandle handle,const Vec3& destination){if(auto* agent=ResolveNavAgent(handle))return agent->SetDestination(destination);return false;}
void NavigationStop(ActorHandle handle){if(auto* agent=ResolveNavAgent(handle))agent->Stop();}
bool NavigationHasPath(ActorHandle handle){if(auto* agent=ResolveNavAgent(handle))return agent->HasPath();return false;}
bool NavigationReached(ActorHandle handle){if(auto* agent=ResolveNavAgent(handle))return agent->ReachedDestination();return false;}
std::string NavigationFindPathJson(const Vec3& start,const Vec3& goal){Scene* scene=ActiveScene();if(!scene)return "[]";std::vector<Vec3> path;if(!scene->GetNavigationWorld().FindPath(start,goal,path))return "[]";nlohmann::json value=nlohmann::json::array();for(const Vec3& p:path)value.push_back({p.x,p.y,p.z});return value.dump();}
bool NavigationSetAreaBlocked(const Vec3& minimum,const Vec3& maximum,bool blocked){Scene* scene=ActiveScene();return scene&&scene->GetNavigationWorld().SetAreaWalkable({minimum,maximum},!blocked);}
void PerceptionEmitSound(const Vec3& position,float radius,ActorHandle source){if(Scene* scene=ActiveScene())scene->GetNavigationWorld().EmitSound(position,radius,source);}
bool PerceptionCanSee(ActorHandle observer,ActorHandle target,float range){Scene* scene=ActiveScene();Actor* a=scene?scene->TryGetActor(observer):nullptr;Actor* b=scene?scene->TryGetActor(target):nullptr;if(!a||!b||range<0)return false;Vec3 delta=b->GetWorldPosition()-a->GetWorldPosition();float distance=delta.Length();if(distance>range)return false;RaycastHit hit;return !scene->GetPhysicsWorld().Raycast(*scene,Ray{a->GetWorldPosition()+Vec3(0,1,0),delta.Normalized()},distance,0xffffffffu,hit)||hit.actor==b;}
EnemyAIComponent* ResolveEnemyAI(ActorHandle handle){Actor* actor=ResolveActorOrSelf(handle);return actor?actor->GetComponent<EnemyAIComponent>():nullptr;}
bool EnemySetTarget(ActorHandle enemy,ActorHandle target){if(!ResolveActor(target))return false;if(auto* ai=ResolveEnemyAI(enemy)){ai->SetTarget(target);return true;}return false;}
int EnemyGetState(ActorHandle enemy){if(auto* ai=ResolveEnemyAI(enemy))return static_cast<int>(ai->GetState());return -1;}
void EnemyStagger(ActorHandle enemy,float duration){if(auto* ai=ResolveEnemyAI(enemy))ai->Stagger(duration);}
bool ScenesLoad(const std::string& path){Scene* scene=ActiveScene();SceneManager* manager=scene?scene->GetSceneManager():nullptr;return manager&&manager->RequestLoad(path);}
int ScenesGetLoadState(){Scene* scene=ActiveScene();SceneManager* manager=scene?scene->GetSceneManager():nullptr;return manager?static_cast<int>(manager->GetState()):static_cast<int>(SceneLoadState::Failed);}
std::string ScenesGetLastError(){Scene* scene=ActiveScene();SceneManager* manager=scene?scene->GetSceneManager():nullptr;return manager?manager->GetLastError():std::string("scene manager unavailable");}
bool ScenesSetPersistentJson(const std::string& key,const std::string& json){Scene* scene=ActiveScene();SceneManager* manager=scene?scene->GetSceneManager():nullptr;if(!manager||key.empty())return false;try{manager->SetPersistentValue(key,nlohmann::json::parse(json));return true;}catch(...){return false;}}
std::string ScenesGetPersistentJson(const std::string& key){Scene* scene=ActiveScene();SceneManager* manager=scene?scene->GetSceneManager():nullptr;return manager?manager->GetPersistentValue(key,nlohmann::json::object()).dump():"{}";}
bool SaveGameWrite(const std::string& slot,const std::string& checkpoint,const std::string& player,const std::string& collected,const std::string& settings){try{SaveGameData data;data.checkpoint=checkpoint;data.player=nlohmann::json::parse(player.empty()?"{}":player);data.collected=nlohmann::json::parse(collected.empty()?"[]":collected).get<std::vector<std::string>>();data.settings=nlohmann::json::parse(settings.empty()?"{}":settings);return SaveGame::Write(slot,data);}catch(...){return false;}}
std::string SaveGameReadJson(const std::string& slot){SaveGameData data;if(!SaveGame::Read(slot,data))return "{}";return SaveGame::ToJson(data).dump();}
bool SaveGameExists(const std::string& slot){return SaveGame::Exists(slot);}bool SaveGameRemove(const std::string& slot){return SaveGame::Remove(slot);}
void GamePause(){if(Scene* scene=ActiveScene())scene->Pause();}void GameResume(){if(Scene* scene=ActiveScene())scene->Resume();}bool GameIsPaused(){Scene* scene=ActiveScene();return scene&&scene->GetState()==SceneState::Paused;}void GameSetTimeScale(float scale){if(Scene* scene=ActiveScene())scene->SetTimeScale(std::clamp(scale,0.0f,10.0f));}float GameGetTimeScale(){Scene* scene=ActiveScene();return scene?scene->GetTimeScale():1.0f;}

void AnimatorSetPlaying(ActorHandle handle, bool playing)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) renderer->SetPlaying(playing);
}

bool AnimatorIsPlaying(ActorHandle handle)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) return renderer->IsPlaying();
    return false;
}

float AnimatorGetTime(ActorHandle handle)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) return renderer->GetAnimationTime();
    return 0.0f;
}

void AnimatorSetTime(ActorHandle handle, float time)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) renderer->SetAnimationTime(time);
}

void AnimatorSetBlendWeight(ActorHandle handle, float weight)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) renderer->SetBlendWeight(weight);
}

float AnimatorGetBlendWeight(ActorHandle handle)
{
    if (auto* renderer = ResolveSkinnedMeshRenderer(handle)) return renderer->GetBlendWeight();
    return 0.0f;
}

bool ScriptSetEnabled(ActorHandle handle, bool enabled)
{
    Actor* actor = ResolveActor(handle);
    ScriptComponent* script = actor ? actor->GetComponent<ScriptComponent>() : nullptr;
    if (!script) return false;
    script->SetEnabled(enabled);
    return true;
}

bool ScriptIsEnabled(ActorHandle handle)
{
    Actor* actor = ResolveActor(handle);
    ScriptComponent* script = actor ? actor->GetComponent<ScriptComponent>() : nullptr;
    return script && script->IsEnabled();
}

UIElementComponent* ResolveUIElement(ActorHandle handle)
{
    Actor* actor = ResolveActorOrSelf(handle);
    if (!actor) return nullptr;
    if (auto* value = actor->GetComponent<UITextComponent>()) return value;
    if (auto* value = actor->GetComponent<UIImageComponent>()) return value;
    if (auto* value = actor->GetComponent<UIButtonComponent>()) return value;
    if (auto* value = actor->GetComponent<UISliderComponent>()) return value;
    if (auto* value = actor->GetComponent<UIProgressBarComponent>()) return value;
    if (auto* value = actor->GetComponent<UIScrollViewComponent>()) return value;
    if (auto* value = actor->GetComponent<UIVerticalLayoutComponent>()) return value;
    if (auto* value = actor->GetComponent<UIHorizontalLayoutComponent>()) return value;
    if (auto* value = actor->GetComponent<UIGridLayoutComponent>()) return value;
    return nullptr;
}

std::string UIElementGetId(ActorHandle handle) { if (auto* element = ResolveUIElement(handle)) return element->GetElementID(); return {}; }
void UIElementSetId(ActorHandle handle, const std::string& value) { if (auto* element = ResolveUIElement(handle)) element->SetElementID(value); }
void UIElementSetText(ActorHandle handle, const std::string& value)
{
    Actor* actor = ResolveActorOrSelf(handle);
    if (!actor) return;
    if (auto* text = actor->GetComponent<UITextComponent>()) text->text = value;
    if (auto* button = actor->GetComponent<UIButtonComponent>()) button->text = value;
}

bool AssetsExists(const std::string& path)
{
    if (path.empty()) return false;
    const std::string resolved = AssetManager::Get().ResolvePath(path);
    return AssetManager::Get().IsLoaded(path) ||
           RuntimeFileSystem::Get().Exists(resolved) ||
           RuntimeFileSystem::Get().Exists(path);
}

std::string ComponentsGetPropertyJson(ActorHandle handle, const std::string& type,
                                      const std::string& property)
{
    Actor* actor = ResolveActor(handle);
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    const TypeDescriptor* descriptor = TypeRegistry::Get().Find(type);
    const PropertyDescriptor* field = descriptor ? TypeRegistry::Get().FindProperty(*descriptor, property) : nullptr;
    if (!component || !field || !HasPropertyFlag(field->flags, PropertyFlags::ScriptRead)) return {};
    nlohmann::json value;
    return TypeRegistry::Get().GetProperty(*component, property, value) ? value.dump() : std::string{};
}

bool ComponentsSetPropertyJson(ActorHandle handle, const std::string& type,
                               const std::string& property, const std::string& json)
{
    Actor* actor = ResolveActor(handle);
    Component* component = actor ? actor->GetComponentByTypeName(type) : nullptr;
    const TypeDescriptor* descriptor = TypeRegistry::Get().Find(type);
    const PropertyDescriptor* field = descriptor ? TypeRegistry::Get().FindProperty(*descriptor, property) : nullptr;
    if (!component || !field || !HasPropertyFlag(field->flags, PropertyFlags::ScriptWrite)) return false;
    try { return TypeRegistry::Get().SetProperty(*component, property, nlohmann::json::parse(json)); }
    catch (...) { return false; }
}

std::string AssetsResolveProjectPath(const std::string& path)
{
    return AssetManager::Get().MakeProjectRelativePath(AssetManager::Get().ResolvePath(path));
}

std::string AssetsGetType(const std::string& path)
{
    if (auto asset = AssetManager::Get().GetByPath<Asset>(path); asset.IsValid()) {
        return AssetTypeToString(asset->GetType());
    }
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return "Texture";
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb") return "Model";
    if (ext == ".mat" || ext == ".material") return "Material";
    if (ext == ".shader") return "Shader";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return "AudioClip";
    if (ext == ".as") return "Script";
    const std::string prefabSuffix = ".prefab.json";
    if (path.size() >= prefabSuffix.size() &&
        path.substr(path.size() - prefabSuffix.size()) == prefabSuffix) return "Prefab";
    return "Unknown";
}

std::unordered_set<std::string> g_DebugOnceKeys;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_DebugThrottleTimes;

std::string DebugPrefix(const std::string& category)
{
    return category.empty() ? std::string{"[Script] "} : "[Script:" + category + "] ";
}

void DebugLog(const std::string& message) { Logger::Info("[Script] ", message); }
void DebugWarning(const std::string& message) { Logger::Warn("[Script] ", message); }
void DebugError(const std::string& message) { Logger::Error("[Script] ", message); }
void DebugLogCategory(const std::string& category, const std::string& message) { Logger::Info(DebugPrefix(category), message); }
void DebugWarningCategory(const std::string& category, const std::string& message) { Logger::Warn(DebugPrefix(category), message); }
void DebugErrorCategory(const std::string& category, const std::string& message) { Logger::Error(DebugPrefix(category), message); }
void DebugLogOnce(const std::string& key, const std::string& message)
{
    if (key.empty()) { DebugLog(message); return; }
    if (g_DebugOnceKeys.insert(key).second) DebugLog(message);
}
void DebugLogThrottle(const std::string& key, const std::string& message, float seconds)
{
    if (key.empty() || seconds <= 0.0f) { DebugLog(message); return; }
    const auto now = std::chrono::steady_clock::now();
    const auto found = g_DebugThrottleTimes.find(key);
    if (found != g_DebugThrottleTimes.end() &&
        std::chrono::duration<float>(now - found->second).count() < seconds) {
        return;
    }
    g_DebugThrottleTimes[key] = now;
    DebugLog(message);
}
void DebugDrawLine(const Vec3&, const Vec3&, const Vec3&, float) {}
void DebugDrawSphere(const Vec3&, float, const Vec3&, float) {}
void DebugDrawText(const Vec3&, const std::string&, const Vec3&, float) {}

std::string ProfilerGetScriptStatsJson()
{
    return ScriptProfiler::GetStatsJson();
}

void ProfilerResetScriptStats()
{
    ScriptProfiler::Reset();
}

bool IsSafeSaveDataPath(const std::string& path)
{
    if (path.empty()) return false;
    const std::filesystem::path input(path);
    if (input.is_absolute()) return false;
    for (const auto& part : input) {
        if (part == "..") return false;
    }
    return true;
}

std::filesystem::path SaveDataRoot()
{
    const auto& projectRoot = AssetManager::Get().GetProjectRoot();
    const std::filesystem::path root = projectRoot.empty() ? std::filesystem::current_path() : projectRoot;
    return root / "Saved" / "ScriptData";
}

std::filesystem::path ResolveSaveDataPath(const std::string& path)
{
    if (!IsSafeSaveDataPath(path)) return {};
    std::error_code error;
    return std::filesystem::absolute(SaveDataRoot() / std::filesystem::path(path), error).lexically_normal();
}

bool SaveDataExists(const std::string& path)
{
    const std::filesystem::path resolved = ResolveSaveDataPath(path);
    if (resolved.empty()) return false;
    std::error_code error;
    return std::filesystem::is_regular_file(resolved, error);
}

std::string SaveDataReadJson(const std::string& path)
{
    const std::filesystem::path resolved = ResolveSaveDataPath(path);
    if (resolved.empty()) return "{}";
    std::ifstream input(resolved, std::ios::binary);
    if (!input) return "{}";
    std::ostringstream stream;
    stream << input.rdbuf();
    try {
        const nlohmann::json json = nlohmann::json::parse(stream.str());
        return json.dump();
    } catch (...) {
        return "{}";
    }
}

bool SaveDataWriteJson(const std::string& path, const std::string& json)
{
    const std::filesystem::path resolved = ResolveSaveDataPath(path);
    if (resolved.empty()) return false;
    try {
        const nlohmann::json parsed = nlohmann::json::parse(json.empty() ? "{}" : json);
        std::filesystem::create_directories(resolved.parent_path());
        std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << parsed.dump(2);
        return true;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] save data write failed: ", e.what());
        return false;
    } catch (...) {
        Logger::Warn("[Scripting] save data write failed with an unknown error");
        return false;
    }
}

bool SaveDataDelete(const std::string& path)
{
    const std::filesystem::path resolved = ResolveSaveDataPath(path);
    if (resolved.empty()) return false;
    std::error_code error;
    return std::filesystem::remove(resolved, error);
}

bool InputActionDown(const std::string& action) { return Input::IsActionDown(action); }
bool InputActionPressed(const std::string& action) { return Input::IsActionPressed(action); }
bool InputActionReleased(const std::string& action) { return Input::IsActionReleased(action); }
float InputAxis(const std::string& action) { return Input::GetAxis1D(action); }
Math::Vec2 InputAxis2(const std::string& action) { return Input::GetAxis2D(action); }
bool InputKeyDown(int scancode) { return Input::IsKeyDown(scancode); }
bool InputKeyPressed(int scancode) { return Input::IsKeyPressed(scancode); }
bool InputKeyReleased(int scancode) { return Input::IsKeyReleased(scancode); }
bool InputMouseDown(int button) { return Input::IsMouseDown(button); }
bool InputMousePressed(int button) { return Input::IsMousePressed(button); }
bool InputMouseReleased(int button) { return Input::IsMouseReleased(button); }
Math::Vec2 InputMousePosition()
{
    return Math::Vec2{static_cast<float>(Input::GetMouseX()),
                      static_cast<float>(Input::GetMouseY())};
}
Math::Vec2 InputMouseDelta()
{
    return Math::Vec2{static_cast<float>(Input::GetMouseRelX()),
                      static_cast<float>(Input::GetMouseRelY())};
}
int InputGamepadCount() { return Input::GetGamepadCount(); }
int InputPrimaryGamepadId() { return static_cast<int>(Input::GetPrimaryGamepadId()); }
bool InputGamepadConnected(int id) { return Input::IsGamepadConnected(static_cast<SDL_JoystickID>(id)); }
bool InputGamepadButtonDown(int id, int button)
{
    return Input::IsGamepadButtonDown(static_cast<SDL_JoystickID>(id),
                                      static_cast<SDL_GamepadButton>(button));
}
bool InputGamepadButtonPressed(int id, int button)
{
    return Input::IsGamepadButtonPressed(static_cast<SDL_JoystickID>(id),
                                         static_cast<SDL_GamepadButton>(button));
}
bool InputGamepadButtonReleased(int id, int button)
{
    return Input::IsGamepadButtonReleased(static_cast<SDL_JoystickID>(id),
                                          static_cast<SDL_GamepadButton>(button));
}
float InputGamepadAxis(int id, int axis)
{
    return Input::GetGamepadAxis(static_cast<SDL_JoystickID>(id),
                                 static_cast<SDL_GamepadAxis>(axis));
}

void ConstructActorHandle(void* memory) { new (memory) ActorHandle(); }
void ConstructActorHandle(uint64_t value, void* memory) { new (memory) ActorHandle(ActorHandle::FromUInt64(value)); }
bool ActorHandleIsValid(const ActorHandle& handle) { return handle.IsValid(); }
uint64_t ActorHandleToUInt64(const ActorHandle& handle) { return handle.ToUInt64(); }
ActorHandle ActorHandleFromUInt64(uint64_t value) { return ActorHandle::FromUInt64(value); }
bool ActorHandleEquals(const ActorHandle& a, const ActorHandle& b) { return a == b; }

void ReturnActorHandle(asIScriptGeneric* generic, ActorHandle handle)
{
    new (generic->GetAddressOfReturnLocation()) ActorHandle(handle);
}

void ActorHandleFromUInt64Generic(asIScriptGeneric* generic)
{
    ReturnActorHandle(generic, ActorHandle::FromUInt64(generic->GetArgQWord(0)));
}

void ActorHandleArrayAtGeneric(asIScriptGeneric* generic)
{
    const auto* array = static_cast<const ScriptUInt64Array*>(generic->GetObject());
    ReturnActorHandle(generic, ActorHandleArrayAt(generic->GetArgDWord(0), array));
}

void SceneGetSelfGeneric(asIScriptGeneric* generic)
{
    ReturnActorHandle(generic, SceneGetSelf());
}

void SceneFindByNameGeneric(asIScriptGeneric* generic)
{
    const auto* name = static_cast<const std::string*>(generic->GetArgObject(0));
    ReturnActorHandle(generic, SceneFindByName(name ? *name : std::string{}));
}

void SceneFindByTagGeneric(asIScriptGeneric* generic)
{
    const auto* tag = static_cast<const std::string*>(generic->GetArgObject(0));
    ReturnActorHandle(generic, SceneFindByTag(tag ? *tag : std::string{}));
}

void SceneGetParentGeneric(asIScriptGeneric* generic)
{
    const auto* handle = static_cast<const ActorHandle*>(generic->GetArgObject(0));
    ReturnActorHandle(generic, SceneGetParent(handle ? *handle : ActorHandle{}));
}

void SceneCreateActorGeneric(asIScriptGeneric* generic)
{
    const auto* name = static_cast<const std::string*>(generic->GetArgObject(0));
    ReturnActorHandle(generic, SceneCreateActor(name ? *name : std::string{}));
}

void SceneInstantiatePrefabGeneric(asIScriptGeneric* generic)
{
    const auto* path = static_cast<const std::string*>(generic->GetArgObject(0));
    const auto* position = static_cast<const Vec3*>(generic->GetArgObject(1));
    const auto* rotation = static_cast<const Vec3*>(generic->GetArgObject(2));
    ReturnActorHandle(generic, SceneInstantiatePrefab(
        path ? *path : std::string{},
        position ? *position : Vec3{},
        rotation ? *rotation : Vec3{}));
}

void SceneFindNearestWithComponentGeneric(asIScriptGeneric* generic)
{
    const auto* type = static_cast<const std::string*>(generic->GetArgObject(0));
    const auto* center = static_cast<const Vec3*>(generic->GetArgObject(1));
    ReturnActorHandle(generic, SceneFindNearestWithComponent(
        type ? *type : std::string{},
        center ? *center : Vec3{},
        generic->GetArgFloat(2)));
}

void InteractionFindNearestGeneric(asIScriptGeneric* generic)
{
    const auto* instigator=static_cast<const ActorHandle*>(generic->GetArgObject(0));
    ReturnActorHandle(generic,InteractionFindNearest(instigator?*instigator:ActorHandle{},generic->GetArgFloat(1)));
}

void InteractionFindNearestSelfGeneric(asIScriptGeneric* generic)
{
    ReturnActorHandle(generic,InteractionFindNearest(ActiveActorHandle(),generic->GetArgFloat(0)));
}

bool InteractionUseRef(const ActorHandle& target,const ActorHandle& instigator){return InteractionUse(target,instigator);}
bool InteractionUseSelfRef(const ActorHandle& target){return InteractionUse(target,ActiveActorHandle());}
std::string InteractionGetPromptRef(const ActorHandle& target){return InteractionGetPrompt(target);}

void PrefabInstanceGetRootGeneric(asIScriptGeneric* generic)
{
    const auto* handle = static_cast<const ActorHandle*>(generic->GetArgObject(0));
    ReturnActorHandle(generic, PrefabInstanceGetRoot(handle ? *handle : ActorHandle{}));
}

struct ScriptRaycastHit {
    uint64_t actorHandle = 0;
    float distance = 0.0f;
    Vec3 point;
    Vec3 normal;
    bool hit = false;
};

ScriptRaycastHit PhysicsRaycast(const Vec3& origin, const Vec3& direction, float distance, uint32_t mask)
{
    ScriptRaycastHit result;
    Actor* actor = ActiveActor();
    Scene* scene = actor ? actor->GetScene() : nullptr;
    if (!scene) return result;
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    RaycastHit hit;
    if (!scene->GetPhysicsWorld().Raycast(*scene, ray, distance, mask, hit)) return result;
    result.hit = true;
    result.actorHandle = hit.actor ? hit.actor->GetHandle().ToUInt64() : 0;
    result.distance = hit.distance;
    result.point = hit.point;
    result.normal = hit.normal;
    return result;
}

ScriptUInt64Array* PhysicsOverlapSphere(const Vec3& center, float radius, uint32_t mask)
{
    std::vector<ActorHandle> handles;
    Actor* actor = ActiveActor();
    Scene* scene = actor ? actor->GetScene() : nullptr;
    if (scene) scene->GetPhysicsWorld().OverlapSphere(*scene, center, radius, mask, handles);
    std::vector<uint64_t> values;
    values.reserve(handles.size());
    for (const ActorHandle& handle : handles) values.push_back(handle.ToUInt64());
    return new ScriptUInt64Array(std::move(values));
}

struct ScriptCollisionEvent {
    uint64_t otherHandle = 0;
    Vec3 point;
    Vec3 normal;
    float depth = 0.0f;
    bool trigger = false;
    int phase = 0;
};

struct ScriptUIEvent {
    std::string elementId;
    std::string eventName;
    float value = 0.0f;
    bool hasValue = false;
};

struct QueuedScriptEvent {
    Scene* scene = nullptr;
    std::string name;
    std::string payload;
};

struct ScriptEventSubscription {
    void* owner = nullptr;
    Scene* scene = nullptr;
    std::string name;
    std::function<bool(const std::string&, std::string&)> callback;
};

std::vector<QueuedScriptEvent> g_QueuedScriptEvents;
std::vector<ScriptEventSubscription> g_ScriptEventSubscriptions;

void ConstructUIEvent(void* memory) { new (memory) ScriptUIEvent(); }
void DestructUIEvent(ScriptUIEvent* event) { event->~ScriptUIEvent(); }

bool UISubscribe(const std::string& elementId, const std::string& eventName,
                 const std::string& callbackName)
{
    ScriptComponent* component = ActiveComponent();
    return component && component->SubscribeUIEvent(elementId, eventName, callbackName);
}

void UIUnsubscribe(const std::string& elementId, const std::string& eventName)
{
    if (ScriptComponent* component = ActiveComponent()) {
        component->UnsubscribeUIEvent(elementId, eventName);
    }
}

void UIClearSubscriptions()
{
    if (ScriptComponent* component = ActiveComponent()) {
        component->ClearUIEventSubscriptions();
    }
}

void UISetBool(const std::string& model, const std::string& key, bool value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetBool(key, value);
}

void UISetInt(const std::string& model, const std::string& key, int value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetInt(key, value);
}

void UISetFloat(const std::string& model, const std::string& key, float value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetFloat(key, value);
}

void UISetString(const std::string& model, const std::string& key, const std::string& value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetString(key, value);
}

const UIDataModel::Value* UIGetValue(const std::string& model, const std::string& key)
{
    auto* uiSystem = UISystemBinding();
    if (!uiSystem) return nullptr;
    const auto& values = uiSystem->CreateDataModel(model).GetValues();
    const auto found = values.find(key);
    return found == values.end() ? nullptr : &found->second;
}

bool UIGetBool(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<bool>(*value) ? std::get<bool>(*value) : false;
}

int UIGetInt(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<int>(*value) ? std::get<int>(*value) : 0;
}

float UIGetFloat(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<float>(*value) ? std::get<float>(*value) : 0.0f;
}

std::string UIGetString(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<std::string>(*value) ? std::get<std::string>(*value) : std::string{};
}

void UISetVec2(const std::string& model, const std::string& key, const Vec2& value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetVec2(key, value);
}

void UISetVec3(const std::string& model, const std::string& key, const Vec3& value)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).SetVec3(key, value);
}

void UISetJson(const std::string& model, const std::string& key, const std::string& json)
{
    auto* uiSystem = UISystemBinding();
    if (!uiSystem) return;
    try {
        uiSystem->CreateDataModel(model).SetJson(
            key, nlohmann::json::parse(json.empty() ? "[]" : json));
    } catch (...) {
        uiSystem->CreateDataModel(model).SetJson(key, nlohmann::json::array());
    }
}

Vec2 UIGetVec2(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<Vec2>(*value) ? std::get<Vec2>(*value) : Vec2{};
}

Vec3 UIGetVec3(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<Vec3>(*value) ? std::get<Vec3>(*value) : Vec3{};
}

std::string UIGetJson(const std::string& model, const std::string& key)
{
    const auto* value = UIGetValue(model, key);
    return value && std::holds_alternative<nlohmann::json>(*value) ? std::get<nlohmann::json>(*value).dump() : "[]";
}

void UINotify(const std::string& model, const std::string&)
{
    if (auto* uiSystem = UISystemBinding()) uiSystem->CreateDataModel(model).MarkDirty();
}

bool EventsSubscribe(const std::string& name, const std::string& callbackName)
{
    try {
        ScriptComponent* component = ActiveComponent();
        return component && component->SubscribeScriptEvent(name, callbackName);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] event subscribe failed: ", e.what());
        return false;
    } catch (...) {
        Logger::Warn("[Scripting] event subscribe failed with an unknown error");
        return false;
    }
}

bool EventsEmit(const std::string& name, const std::string& payload)
{
    try {
        ScriptComponent* component = ActiveComponent();
        return component && component->EmitScriptEvent(name, payload);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] event emit failed: ", e.what());
        return false;
    } catch (...) {
        Logger::Warn("[Scripting] event emit failed with an unknown error");
        return false;
    }
}

uint64_t TimerAfter(float seconds, const std::string& callbackName)
{
    try {
        ScriptComponent* component = ActiveComponent();
        return component ? component->ScheduleTimer(seconds, false, callbackName) : 0;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] timer schedule failed: ", e.what());
        return 0;
    } catch (...) {
        Logger::Warn("[Scripting] timer schedule failed with an unknown error");
        return 0;
    }
}

uint64_t TimerEvery(float seconds, const std::string& callbackName)
{
    try {
        ScriptComponent* component = ActiveComponent();
        return component ? component->ScheduleTimer(seconds, true, callbackName) : 0;
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] timer schedule failed: ", e.what());
        return 0;
    } catch (...) {
        Logger::Warn("[Scripting] timer schedule failed with an unknown error");
        return 0;
    }
}

void TimerCancel(uint64_t id)
{
    try {
        if (ScriptComponent* component = ActiveComponent()) component->CancelTimer(id);
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] timer cancel failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] timer cancel failed with an unknown error");
    }
}

void TimerCancelAll()
{
    try {
        if (ScriptComponent* component = ActiveComponent()) component->CancelAllTimers();
    } catch (const std::exception& e) {
        Logger::Warn("[Scripting] timer cancel failed: ", e.what());
    } catch (...) {
        Logger::Warn("[Scripting] timer cancel failed with an unknown error");
    }
}

uint64_t TaskDelay(float seconds, const std::string& callbackName)
{
    return TimerAfter(seconds, callbackName);
}

void TaskCancel(uint64_t id)
{
    TimerCancel(id);
}

void TaskCancelAll()
{
    TimerCancelAll();
}

void ConstructVec2(void* memory) { new (memory) Math::Vec2(); }
void ConstructVec2(float x, float y, void* memory) { new (memory) Math::Vec2{x, y}; }
void ConstructVec3(void* memory) { new (memory) Vec3(); }
void ConstructVec3(float x, float y, float z, void* memory) { new (memory) Vec3{x, y, z}; }
void ConstructRaycastHit(void* memory) { new (memory) ScriptRaycastHit(); }
void ConstructCollisionEvent(void* memory) { new (memory) ScriptCollisionEvent(); }
Math::Vec2 Vec2Add(const Math::Vec2& self, const Math::Vec2& other) { return self + other; }
Math::Vec2 Vec2Sub(const Math::Vec2& self, const Math::Vec2& other) { return self - other; }
Math::Vec2 Vec2Mul(const Math::Vec2& self, float scalar) { return self * scalar; }
Vec3 Vec3Add(const Vec3& self, const Vec3& other) { return self + other; }
Vec3 Vec3Sub(const Vec3& self, const Vec3& other) { return self - other; }
Vec3 Vec3Mul(const Vec3& self, float scalar) { return self * scalar; }

bool Check(int result)
{
    return result >= 0;
}

ScriptFieldType FieldTypeFromAngelScript(asIScriptEngine& engine, int typeId)
{
    if (typeId == asTYPEID_BOOL) return ScriptFieldType::Bool;
    if (typeId == asTYPEID_INT8 || typeId == asTYPEID_INT16 ||
        typeId == asTYPEID_INT32 || typeId == asTYPEID_INT64) {
        return ScriptFieldType::Int;
    }
    if (typeId == asTYPEID_UINT8 || typeId == asTYPEID_UINT16 ||
        typeId == asTYPEID_UINT32 || typeId == asTYPEID_UINT64) {
        return ScriptFieldType::UInt;
    }
    if (typeId == asTYPEID_FLOAT) return ScriptFieldType::Float;
    if (typeId == asTYPEID_DOUBLE) return ScriptFieldType::Double;

    if (asITypeInfo* info = engine.GetTypeInfoById(typeId)) {
        const std::string name = info->GetName() ? info->GetName() : "";
        if (name.find("string") != std::string::npos) return ScriptFieldType::String;
        if (name == "Vec2") return ScriptFieldType::Vec2;
        if (name == "Vec3") return ScriptFieldType::Vec3;
    }
    const std::string declaration = engine.GetTypeDeclaration(typeId, true);
    if (declaration.find("string") != std::string::npos) return ScriptFieldType::String;
    if (declaration == "Vec2") return ScriptFieldType::Vec2;
    if (declaration == "Vec3") return ScriptFieldType::Vec3;
    return ScriptFieldType::Unsupported;
}

nlohmann::json ReadFieldJson(asIScriptObject& object, asITypeInfo& type,
                             unsigned int index, ScriptFieldType fieldType)
{
    void* address = object.GetAddressOfProperty(index);
    if (!address) return nlohmann::json();

    int typeId = 0;
    type.GetProperty(index, nullptr, &typeId);
    switch (fieldType) {
        case ScriptFieldType::Bool: return *static_cast<bool*>(address);
        case ScriptFieldType::Int:
            if (typeId == asTYPEID_INT8) return *static_cast<int8_t*>(address);
            if (typeId == asTYPEID_INT16) return *static_cast<int16_t*>(address);
            if (typeId == asTYPEID_INT64) return *static_cast<int64_t*>(address);
            return *static_cast<int32_t*>(address);
        case ScriptFieldType::UInt:
            if (typeId == asTYPEID_UINT8) return *static_cast<uint8_t*>(address);
            if (typeId == asTYPEID_UINT16) return *static_cast<uint16_t*>(address);
            if (typeId == asTYPEID_UINT64) return *static_cast<uint64_t*>(address);
            return *static_cast<uint32_t*>(address);
        case ScriptFieldType::Float: return *static_cast<float*>(address);
        case ScriptFieldType::Double: return *static_cast<double*>(address);
        case ScriptFieldType::String: return *static_cast<std::string*>(address);
        case ScriptFieldType::Vec2: {
            const auto& value = *static_cast<Math::Vec2*>(address);
            return nlohmann::json::array({ value.x, value.y });
        }
        case ScriptFieldType::Vec3: {
            const auto& value = *static_cast<Vec3*>(address);
            return nlohmann::json::array({ value.x, value.y, value.z });
        }
        default:
            return nlohmann::json();
    }
}

bool WriteFieldJson(asIScriptObject& object, asITypeInfo& type,
                    unsigned int index, ScriptFieldType fieldType,
                    const nlohmann::json& value)
{
    void* address = object.GetAddressOfProperty(index);
    if (!address) return false;

    int typeId = 0;
    type.GetProperty(index, nullptr, &typeId);
    try {
        switch (fieldType) {
            case ScriptFieldType::Bool:
                if (value.is_boolean()) { *static_cast<bool*>(address) = value.get<bool>(); return true; }
                return false;
            case ScriptFieldType::Int:
                if (!value.is_number_integer()) return false;
                if (typeId == asTYPEID_INT8) *static_cast<int8_t*>(address) = static_cast<int8_t>(value.get<int64_t>());
                else if (typeId == asTYPEID_INT16) *static_cast<int16_t*>(address) = static_cast<int16_t>(value.get<int64_t>());
                else if (typeId == asTYPEID_INT64) *static_cast<int64_t*>(address) = value.get<int64_t>();
                else *static_cast<int32_t*>(address) = static_cast<int32_t>(value.get<int64_t>());
                return true;
            case ScriptFieldType::UInt:
                if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
                if (typeId == asTYPEID_UINT8) *static_cast<uint8_t*>(address) = static_cast<uint8_t>(value.get<uint64_t>());
                else if (typeId == asTYPEID_UINT16) *static_cast<uint16_t*>(address) = static_cast<uint16_t>(value.get<uint64_t>());
                else if (typeId == asTYPEID_UINT64) *static_cast<uint64_t*>(address) = value.get<uint64_t>();
                else *static_cast<uint32_t*>(address) = static_cast<uint32_t>(value.get<uint64_t>());
                return true;
            case ScriptFieldType::Float:
                if (value.is_number()) { *static_cast<float*>(address) = value.get<float>(); return true; }
                return false;
            case ScriptFieldType::Double:
                if (value.is_number()) { *static_cast<double*>(address) = value.get<double>(); return true; }
                return false;
            case ScriptFieldType::String:
                if (value.is_string()) { *static_cast<std::string*>(address) = value.get<std::string>(); return true; }
                return false;
            case ScriptFieldType::Vec2:
                if (value.is_array() && value.size() >= 2 && value[0].is_number() && value[1].is_number()) {
                    *static_cast<Math::Vec2*>(address) = Math::Vec2{ value[0].get<float>(), value[1].get<float>() };
                    return true;
                }
                return false;
            case ScriptFieldType::Vec3:
                if (value.is_array() && value.size() >= 3 && value[0].is_number() &&
                    value[1].is_number() && value[2].is_number()) {
                    *static_cast<Vec3*>(address) = Vec3{ value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
                    return true;
                }
                return false;
            default:
                return false;
        }
    } catch (...) {
        return false;
    }
}

std::vector<ScriptFieldInfo> ReflectFields(asIScriptEngine& engine, asITypeInfo& type,
                                           asIScriptObject* object)
{
    std::vector<ScriptFieldInfo> fields;
    const unsigned int count = type.GetPropertyCount();
    for (unsigned int i = 0; i < count; ++i) {
        const char* name = nullptr;
        int typeId = 0;
        bool isPrivate = false;
        bool isProtected = false;
        type.GetProperty(i, &name, &typeId, &isPrivate, &isProtected);
        if (!name || isPrivate || isProtected) continue;

        ScriptFieldInfo field;
        field.name = name;
        field.declaration = type.GetPropertyDeclaration(i, true);
        field.type = FieldTypeFromAngelScript(engine, typeId);
        if (field.type == ScriptFieldType::Unsupported) continue;
        if (object) field.defaultValue = ReadFieldJson(*object, type, i, field.type);
        fields.push_back(std::move(field));
    }
    return fields;
}

int FindPropertyIndexByName(asITypeInfo& type, const std::string& name)
{
    const unsigned int count = type.GetPropertyCount();
    for (unsigned int i = 0; i < count; ++i) {
        const char* propertyName = nullptr;
        type.GetProperty(i, &propertyName);
        if (propertyName && name == propertyName) return static_cast<int>(i);
    }
    return -1;
}

void CaptureProperties(asIScriptObject& object, asITypeInfo& type,
                       const std::vector<ScriptFieldInfo>& fields,
                       nlohmann::json& properties)
{
    properties = nlohmann::json::object();
    for (const auto& field : fields) {
        const int index = FindPropertyIndexByName(type, field.name);
        if (index < 0) continue;
        properties[field.name] = ReadFieldJson(object, type, static_cast<unsigned int>(index), field.type);
    }
}

void ApplyProperties(asIScriptObject& object, asITypeInfo& type,
                     const std::vector<ScriptFieldInfo>& fields,
                     const nlohmann::json& properties)
{
    if (!properties.is_object()) return;
    for (const auto& field : fields) {
        if (!properties.contains(field.name)) continue;
        const int index = FindPropertyIndexByName(type, field.name);
        if (index < 0) continue;
        WriteFieldJson(object, type, static_cast<unsigned int>(index), field.type, properties[field.name]);
    }
}

void RegisterVec2(asIScriptEngine& engine)
{
    Check(engine.RegisterObjectType("Vec2", sizeof(Math::Vec2),
        asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<Math::Vec2>()));
    Check(engine.RegisterObjectBehaviour("Vec2", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructVec2, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("Vec2", asBEHAVE_CONSTRUCT, "void f(float, float)",
        asFUNCTIONPR(ConstructVec2, (float, float, void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("Vec2", "float x", asOFFSET(Math::Vec2, x)));
    Check(engine.RegisterObjectProperty("Vec2", "float y", asOFFSET(Math::Vec2, y)));
#ifdef MYENGINE_PLATFORM_WINDOWS
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opAdd(const Vec2 &in) const",
        asMETHODPR(Math::Vec2, operator+, (const Math::Vec2&) const, Math::Vec2), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opSub(const Vec2 &in) const",
        asMETHODPR(Math::Vec2, operator-, (const Math::Vec2&) const, Math::Vec2), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opMul(float) const",
        asMETHODPR(Math::Vec2, operator*, (float) const, Math::Vec2), asCALL_THISCALL));
#else
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opAdd(const Vec2 &in) const",
        asFUNCTION(Vec2Add), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opSub(const Vec2 &in) const",
        asFUNCTION(Vec2Sub), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opMul(float) const",
        asFUNCTION(Vec2Mul), asCALL_CDECL_OBJFIRST));
#endif
}

void RegisterVec3(asIScriptEngine& engine)
{
    Check(engine.RegisterObjectType("Vec3", sizeof(Vec3),
        asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<Vec3>()));
    Check(engine.RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructVec3, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)",
        asFUNCTIONPR(ConstructVec3, (float, float, float, void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("Vec3", "float x", asOFFSET(Vec3, x)));
    Check(engine.RegisterObjectProperty("Vec3", "float y", asOFFSET(Vec3, y)));
    Check(engine.RegisterObjectProperty("Vec3", "float z", asOFFSET(Vec3, z)));
#ifdef MYENGINE_PLATFORM_WINDOWS
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opAdd(const Vec3 &in) const",
        asMETHODPR(Vec3, operator+, (const Vec3&) const, Vec3), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opSub(const Vec3 &in) const",
        asMETHODPR(Vec3, operator-, (const Vec3&) const, Vec3), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opMul(float) const",
        asMETHODPR(Vec3, operator*, (float) const, Vec3), asCALL_THISCALL));
#else
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opAdd(const Vec3 &in) const",
        asFUNCTION(Vec3Add), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opSub(const Vec3 &in) const",
        asFUNCTION(Vec3Sub), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opMul(float) const",
        asFUNCTION(Vec3Mul), asCALL_CDECL_OBJFIRST));
#endif
}

void RegisterScriptTypes(asIScriptEngine& engine)
{
    RegisterVec2(engine);
    RegisterVec3(engine);

    Check(engine.RegisterObjectType("ActorHandle", sizeof(ActorHandle),
        asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS | asGetTypeTraits<ActorHandle>()));
    Check(engine.RegisterObjectBehaviour("ActorHandle", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructActorHandle, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("ActorHandle", asBEHAVE_CONSTRUCT, "void f(uint64)",
        asFUNCTIONPR(ConstructActorHandle, (uint64_t, void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("ActorHandle", "uint index", asOFFSET(ActorHandle, index)));
    Check(engine.RegisterObjectProperty("ActorHandle", "uint generation", asOFFSET(ActorHandle, generation)));
    Check(engine.RegisterObjectMethod("ActorHandle", "bool IsValid() const",
        asFUNCTION(ActorHandleIsValid), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("ActorHandle", "uint64 ToUInt64() const",
        asFUNCTION(ActorHandleToUInt64), asCALL_CDECL_OBJFIRST));
    Check(engine.RegisterObjectMethod("ActorHandle", "bool opEquals(const ActorHandle &in) const",
        asFUNCTION(ActorHandleEquals), asCALL_CDECL_OBJFIRST));

    Check(engine.RegisterObjectType("UInt64Array", 0, asOBJ_REF));
    Check(engine.RegisterObjectBehaviour("UInt64Array", asBEHAVE_ADDREF, "void f()",
        asFUNCTION(UInt64ArrayAddRef), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("UInt64Array", asBEHAVE_RELEASE, "void f()",
        asFUNCTION(UInt64ArrayRelease), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectMethod("UInt64Array", "uint Length() const",
        asFUNCTION(UInt64ArrayLength), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectMethod("UInt64Array", "uint64 At(uint index) const",
        asFUNCTION(UInt64ArrayAt), asCALL_CDECL_OBJLAST));

    Check(engine.RegisterObjectType("ActorHandleArray", 0, asOBJ_REF));
    Check(engine.RegisterObjectBehaviour("ActorHandleArray", asBEHAVE_ADDREF, "void f()",
        asFUNCTION(UInt64ArrayAddRef), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("ActorHandleArray", asBEHAVE_RELEASE, "void f()",
        asFUNCTION(UInt64ArrayRelease), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectMethod("ActorHandleArray", "uint Length() const",
        asFUNCTION(UInt64ArrayLength), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectMethod("ActorHandleArray", "ActorHandle At(uint index) const",
        asFUNCTION(ActorHandleArrayAtGeneric), asCALL_GENERIC));

    Check(engine.RegisterObjectType("RaycastHit", sizeof(ScriptRaycastHit),
        asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<ScriptRaycastHit>()));
    Check(engine.RegisterObjectBehaviour("RaycastHit", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructRaycastHit, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("RaycastHit", "uint64 actorHandle",
        asOFFSET(ScriptRaycastHit, actorHandle)));
    Check(engine.RegisterObjectProperty("RaycastHit", "float distance",
        asOFFSET(ScriptRaycastHit, distance)));
    Check(engine.RegisterObjectProperty("RaycastHit", "Vec3 point",
        asOFFSET(ScriptRaycastHit, point)));
    Check(engine.RegisterObjectProperty("RaycastHit", "Vec3 normal",
        asOFFSET(ScriptRaycastHit, normal)));
    Check(engine.RegisterObjectProperty("RaycastHit", "bool hit",
        asOFFSET(ScriptRaycastHit, hit)));

    Check(engine.RegisterObjectType("CollisionEvent", sizeof(ScriptCollisionEvent),
        asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<ScriptCollisionEvent>()));
    Check(engine.RegisterObjectBehaviour("CollisionEvent", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructCollisionEvent, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("CollisionEvent", "uint64 otherHandle",
        asOFFSET(ScriptCollisionEvent, otherHandle)));
    Check(engine.RegisterObjectProperty("CollisionEvent", "Vec3 point",
        asOFFSET(ScriptCollisionEvent, point)));
    Check(engine.RegisterObjectProperty("CollisionEvent", "Vec3 normal",
        asOFFSET(ScriptCollisionEvent, normal)));
    Check(engine.RegisterObjectProperty("CollisionEvent", "float depth",
        asOFFSET(ScriptCollisionEvent, depth)));
    Check(engine.RegisterObjectProperty("CollisionEvent", "bool trigger",
        asOFFSET(ScriptCollisionEvent, trigger)));
    Check(engine.RegisterObjectProperty("CollisionEvent", "int phase",
        asOFFSET(ScriptCollisionEvent, phase)));

    Check(engine.RegisterObjectType("UIEvent", sizeof(ScriptUIEvent),
        asOBJ_VALUE | asGetTypeTraits<ScriptUIEvent>()));
    Check(engine.RegisterObjectBehaviour("UIEvent", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructUIEvent, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("UIEvent", asBEHAVE_DESTRUCT, "void f()",
        asFUNCTION(DestructUIEvent), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("UIEvent", "string elementId",
        asOFFSET(ScriptUIEvent, elementId)));
    Check(engine.RegisterObjectProperty("UIEvent", "string eventName",
        asOFFSET(ScriptUIEvent, eventName)));
    Check(engine.RegisterObjectProperty("UIEvent", "float value",
        asOFFSET(ScriptUIEvent, value)));
    Check(engine.RegisterObjectProperty("UIEvent", "bool hasValue",
        asOFFSET(ScriptUIEvent, hasValue)));
}

void RegisterActorTransformScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptActorTransformBindings.cpp"
}

void RegisterSceneScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptSceneBindings.cpp"
}

void RegisterPhysicsScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptPhysicsBindings.cpp"
}

void RegisterGameplayScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptGameplayBindings.cpp"
}

void RegisterNavigationScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptNavigationBindings.cpp"
}

void RegisterAssetsSaveGameScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptAssetsSaveGameBindings.cpp"
}

void RegisterDebugProfilerScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptDebugProfilerBindings.cpp"
}

void RegisterInputScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptInputBindings.cpp"
}

void RegisterUIScriptBindings(asIScriptEngine& engine)
{
#include "Scripting/Bindings/AngelScriptUIBindings.cpp"
}

void RegisterScriptBindings(asIScriptEngine& engine)
{
    RegisterStdString(&engine);
    RegisterScriptTypes(engine);
    RegisterActorTransformScriptBindings(engine);
    RegisterSceneScriptBindings(engine);
    RegisterPhysicsScriptBindings(engine);
    RegisterGameplayScriptBindings(engine);
    RegisterNavigationScriptBindings(engine);
    RegisterAssetsSaveGameScriptBindings(engine);
    RegisterDebugProfilerScriptBindings(engine);
    RegisterInputScriptBindings(engine);
    RegisterUIScriptBindings(engine);
    engine.SetDefaultNamespace("");
}

ScriptCollisionEvent ConvertCollisionEvent(const CollisionEvent& event)
{
    ScriptCollisionEvent result;
    result.otherHandle = event.otherHandle.ToUInt64();
    result.point = event.point;
    result.normal = event.normal;
    result.depth = event.depth;
    result.trigger = event.trigger;
    result.phase = event.phase == CollisionEventPhase::Enter ? 1
        : event.phase == CollisionEventPhase::Stay ? 2 : 3;
    return result;
}

} // namespace

struct AngelScriptRuntime::Impl {
    explicit Impl(ScriptComponent& owner) : component(owner) {}
    ~Impl()
    {
        ClearUIEventSubscriptions();
        ClearScriptEventSubscriptions();
        CancelAllTimers();
        if (object) object->Release();
        if (engine) engine->ShutDownAndRelease();
    }

    void MessageCallback(const asSMessageInfo* message)
    {
        messages << message->section << "(" << message->row << "," << message->col << ") ";
        if (message->type == asMSGTYPE_ERROR) messages << "error: ";
        else if (message->type == asMSGTYPE_WARNING) messages << "warning: ";
        else messages << "info: ";
        messages << message->message << "\n";
    }

    std::string OwnerDescription() const
    {
        std::ostringstream stream;
        stream << "class=" << (type && type->GetName() ? type->GetName() : "<unknown>");
        if (Actor* actor = component.GetOwner()) {
            stream << " actor='" << actor->GetName() << "' handle=" << actor->GetHandle().ToUInt64();
        } else {
            stream << " actor=<none>";
        }
        return stream.str();
    }

    void RecordExecution(asIScriptFunction* function, int result, double elapsedMs,
                         const std::string& error, int line)
    {
        const std::string scriptClass = type && type->GetName() ? type->GetName() : "<unknown>";
        const std::string declaration = function && function->GetDeclaration()
            ? function->GetDeclaration() : "<unknown>";
        ScriptProfiler::Record(scriptClass, declaration, elapsedMs, result != asEXECUTION_FINISHED);
        if (result == asEXECUTION_FINISHED) return;

        ScriptDiagnostic diagnostic;
        diagnostic.severity = ScriptDiagnostic::Severity::Error;
        diagnostic.message = error;
        diagnostic.scriptClass = scriptClass;
        diagnostic.function = declaration;
        diagnostic.line = line;
        component.AddDiagnostic(std::move(diagnostic));
    }

    bool Execute(asIScriptFunction* function, float deltaSeconds, bool hasDelta,
                 const ScriptCollisionEvent* collision, std::string& error)
    {
        if (!function || !object || !engine) return true;
        asIScriptContext* context = engine->CreateContext();
        if (!context) {
            error = "failed to create AngelScript context";
            return false;
        }
        context->Prepare(function);
        context->SetObject(object);
        if (hasDelta) context->SetArgFloat(0, deltaSeconds);
        if (collision) context->SetArgObject(0, const_cast<ScriptCollisionEvent*>(collision));

        const auto begin = std::chrono::steady_clock::now();
        ActiveScriptComponentScope activeScope(component);
        const int result = context->Execute();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

        if (result != asEXECUTION_FINISHED) {
            std::ostringstream stream;
            stream << "AngelScript execution failed [" << OwnerDescription() << "]";
            if (result == asEXECUTION_EXCEPTION) {
                stream << ": " << context->GetExceptionString();
                if (asIScriptFunction* exceptionFunction = context->GetExceptionFunction()) {
                    stream << " at " << exceptionFunction->GetDeclaration()
                           << ":" << context->GetExceptionLineNumber();
                }
            }
            error = stream.str();
            RecordExecution(function, result, elapsedMs, error,
                            result == asEXECUTION_EXCEPTION ? context->GetExceptionLineNumber() : 0);
            context->Release();
            return false;
        }
        RecordExecution(function, result, elapsedMs, {}, 0);
        context->Release();
        return true;
    }

    bool ExecuteUIEvent(asIScriptFunction* function, const UIEvent& event,
                        std::string& error)
    {
        if (!function || !object || !engine) return true;
        ScriptUIEvent scriptEvent;
        scriptEvent.elementId = event.elementId;
        scriptEvent.eventName = event.eventName;
        scriptEvent.value = event.value;
        scriptEvent.hasValue = event.hasValue;

        asIScriptContext* context = engine->CreateContext();
        if (!context) {
            error = "failed to create AngelScript context";
            return false;
        }
        context->Prepare(function);
        context->SetObject(object);
        context->SetArgObject(0, &scriptEvent);

        const auto begin = std::chrono::steady_clock::now();
        ActiveScriptComponentScope activeScope(component);
        const int result = context->Execute();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

        if (result != asEXECUTION_FINISHED) {
            std::ostringstream stream;
            stream << "AngelScript UI event callback failed [" << OwnerDescription() << "]";
            if (result == asEXECUTION_EXCEPTION) {
                stream << ": " << context->GetExceptionString();
                if (asIScriptFunction* exceptionFunction = context->GetExceptionFunction()) {
                    stream << " at " << exceptionFunction->GetDeclaration()
                           << ":" << context->GetExceptionLineNumber();
                }
            }
            error = stream.str();
            RecordExecution(function, result, elapsedMs, error,
                            result == asEXECUTION_EXCEPTION ? context->GetExceptionLineNumber() : 0);
            context->Release();
            return false;
        }
        RecordExecution(function, result, elapsedMs, {}, 0);
        context->Release();
        return true;
    }

    bool ExecuteAnimationEvent(asIScriptFunction* function, const std::string& name,
                               const std::string& payload, std::string& error)
    {
        if (!function || !object || !engine) return true;
        asIScriptContext* context = engine->CreateContext();
        if (!context) {
            error = "failed to create AngelScript context";
            return false;
        }
        context->Prepare(function);
        context->SetObject(object);
        context->SetArgObject(0, const_cast<std::string*>(&name));
        context->SetArgObject(1, const_cast<std::string*>(&payload));

        const auto begin = std::chrono::steady_clock::now();
        ActiveScriptComponentScope activeScope(component);
        const int result = context->Execute();
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();

        if (result != asEXECUTION_FINISHED) {
            error = "AngelScript animation event callback failed [" + OwnerDescription() + "]";
            if (result == asEXECUTION_EXCEPTION) {
                error += std::string(": ") + context->GetExceptionString();
            }
            RecordExecution(function, result, elapsed, error,
                            result == asEXECUTION_EXCEPTION
                                ? context->GetExceptionLineNumber()
                                : 0);
            context->Release();
            return false;
        }
        RecordExecution(function, result, elapsed, {}, 0);
        context->Release();
        return true;
    }

    bool SubscribeUIEvent(const std::string& elementId, const std::string& eventName,
                          const std::string& callbackName, std::string& error)
    {
        if (!UIEventBridgeBinding()) {
            error = "UI event bridge is not available";
            return false;
        }
        if (!type || callbackName.empty()) {
            error = "UI callback name is empty";
            return false;
        }
        const std::string decl = "void " + callbackName + "(const UIEvent &in)";
        asIScriptFunction* function = type->GetMethodByDecl(decl.c_str());
        if (!function) {
            const std::string byValueDecl = "void " + callbackName + "(UIEvent)";
            function = type->GetMethodByDecl(byValueDecl.c_str());
        }
        if (!function) {
            error = "UI callback not found: " + decl;
            error += " [" + OwnerDescription() + "]";
            return false;
        }

        UIEventBridgeBinding()->Unsubscribe(this, elementId, eventName);
        UIEventBridgeBinding()->SubscribeForOwner(this, nullptr, elementId, eventName,
            [this, function](const UIEvent& event) {
                std::string callbackError;
                if (!ExecuteUIEvent(function, event, callbackError)) {
                    component.FailRuntime(callbackError);
                    ClearUIEventSubscriptions();
                    Logger::Error("[Scripting] ", callbackError);
                }
            });
        return true;
    }

    void UnsubscribeUIEvent(const std::string& elementId, const std::string& eventName)
    {
        if (UIEventBridgeBinding()) UIEventBridgeBinding()->Unsubscribe(this, elementId, eventName);
    }

    void ClearUIEventSubscriptions()
    {
        if (UIEventBridgeBinding()) UIEventBridgeBinding()->ClearOwner(this);
    }

    bool ExecuteNoArg(asIScriptFunction* function, const char* label, std::string& error)
    {
        if (!function || !object || !engine) return true;
        asIScriptContext* context = engine->CreateContext();
        if (!context) {
            error = "failed to create AngelScript context";
            return false;
        }
        context->Prepare(function);
        context->SetObject(object);

        const auto begin = std::chrono::steady_clock::now();
        ActiveScriptComponentScope activeScope(component);
        const int result = context->Execute();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

        if (result != asEXECUTION_FINISHED) {
            std::ostringstream stream;
            stream << "AngelScript " << label << " callback failed [" << OwnerDescription() << "]";
            if (result == asEXECUTION_EXCEPTION) {
                stream << ": " << context->GetExceptionString();
                if (asIScriptFunction* exceptionFunction = context->GetExceptionFunction()) {
                    stream << " at " << exceptionFunction->GetDeclaration()
                           << ":" << context->GetExceptionLineNumber();
                }
            }
            error = stream.str();
            RecordExecution(function, result, elapsedMs, error,
                            result == asEXECUTION_EXCEPTION ? context->GetExceptionLineNumber() : 0);
            context->Release();
            return false;
        }
        RecordExecution(function, result, elapsedMs, {}, 0);
        context->Release();
        return true;
    }

    bool ExecuteStringArg(asIScriptFunction* function, const std::string& value,
                          const char* label, std::string& error)
    {
        if (!function || !object || !engine) return true;
        asIScriptContext* context = engine->CreateContext();
        if (!context) {
            error = "failed to create AngelScript context";
            return false;
        }
        context->Prepare(function);
        context->SetObject(object);
        context->SetArgObject(0, const_cast<std::string*>(&value));

        const auto begin = std::chrono::steady_clock::now();
        ActiveScriptComponentScope activeScope(component);
        const int result = context->Execute();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();

        if (result != asEXECUTION_FINISHED) {
            std::ostringstream stream;
            stream << "AngelScript " << label << " callback failed [" << OwnerDescription() << "]";
            if (result == asEXECUTION_EXCEPTION) {
                stream << ": " << context->GetExceptionString();
                if (asIScriptFunction* exceptionFunction = context->GetExceptionFunction()) {
                    stream << " at " << exceptionFunction->GetDeclaration()
                           << ":" << context->GetExceptionLineNumber();
                }
            }
            error = stream.str();
            RecordExecution(function, result, elapsedMs, error,
                            result == asEXECUTION_EXCEPTION ? context->GetExceptionLineNumber() : 0);
            context->Release();
            return false;
        }
        RecordExecution(function, result, elapsedMs, {}, 0);
        context->Release();
        return true;
    }

    bool SubscribeScriptEvent(const std::string& eventName, const std::string& callbackName,
                              std::string& error)
    {
        if (!type || eventName.empty() || callbackName.empty()) {
            error = "script event name or callback is empty";
            return false;
        }
        const std::string decl = "void " + callbackName + "(const string &in)";
        asIScriptFunction* function = type->GetMethodByDecl(decl.c_str());
        if (!function) {
            error = "script event callback not found: " + decl;
            error += " [" + OwnerDescription() + "]";
            return false;
        }
        ClearScriptEventSubscription(eventName);
        g_ScriptEventSubscriptions.push_back({
            this,
            component.GetOwner() ? component.GetOwner()->GetScene() : nullptr,
            eventName,
            [this, function](const std::string& payload, std::string& callbackError) {
                return ExecuteStringArg(function, payload, "event", callbackError);
            }
        });
        return true;
    }

    void ClearScriptEventSubscription(const std::string& eventName)
    {
        g_ScriptEventSubscriptions.erase(
            std::remove_if(g_ScriptEventSubscriptions.begin(), g_ScriptEventSubscriptions.end(),
                [&](const ScriptEventSubscription& subscription) {
                    return subscription.owner == this && subscription.name == eventName;
                }),
            g_ScriptEventSubscriptions.end());
    }

    void ClearScriptEventSubscriptions()
    {
        g_ScriptEventSubscriptions.erase(
            std::remove_if(g_ScriptEventSubscriptions.begin(), g_ScriptEventSubscriptions.end(),
                [&](const ScriptEventSubscription& subscription) {
                    return subscription.owner == this;
                }),
            g_ScriptEventSubscriptions.end());
    }

    bool EmitScriptEvent(const std::string& eventName, const std::string& payload)
    {
        Scene* scene = component.GetOwner() ? component.GetOwner()->GetScene() : nullptr;
        if (!scene || eventName.empty()) return false;
        g_QueuedScriptEvents.push_back({scene, eventName, payload.empty() ? "{}" : payload});
        return true;
    }

    uint64_t ScheduleTimer(float seconds, bool repeat, const std::string& callbackName,
                           std::string& error)
    {
        if (!type || seconds < 0.0f || callbackName.empty()) {
            error = "timer seconds or callback is invalid";
            return 0;
        }
        const std::string decl = "void " + callbackName + "()";
        asIScriptFunction* function = type->GetMethodByDecl(decl.c_str());
        if (!function) {
            error = "timer callback not found: " + decl;
            error += " [" + OwnerDescription() + "]";
            return 0;
        }
        Timer timer;
        timer.id = nextTimerID++;
        timer.interval = std::max(0.0f, seconds);
        timer.remaining = timer.interval;
        timer.repeat = repeat;
        timer.function = function;
        timers.push_back(timer);
        return timer.id;
    }

    void CancelTimer(uint64_t timerID)
    {
        timers.erase(
            std::remove_if(timers.begin(), timers.end(),
                [&](const Timer& timer) { return timer.id == timerID; }),
            timers.end());
    }

    void CancelAllTimers()
    {
        timers.clear();
    }

    bool TickTimers(float deltaSeconds, std::string& error)
    {
        if (timers.empty()) return true;
        std::vector<asIScriptFunction*> due;
        for (Timer& timer : timers) {
            timer.remaining -= deltaSeconds;
            if (timer.remaining <= 0.0f) {
                due.push_back(timer.function);
                if (timer.repeat) {
                    timer.remaining += std::max(0.0001f, timer.interval);
                } else {
                    timer.cancelled = true;
                }
            }
        }
        timers.erase(
            std::remove_if(timers.begin(), timers.end(),
                [](const Timer& timer) { return timer.cancelled; }),
            timers.end());
        for (asIScriptFunction* function : due) {
            if (!ExecuteNoArg(function, "timer", error)) return false;
        }
        return true;
    }

    bool FlushScriptEvents(std::string& error)
    {
        Scene* scene = component.GetOwner() ? component.GetOwner()->GetScene() : nullptr;
        if (!scene || g_QueuedScriptEvents.empty()) return true;
        std::vector<QueuedScriptEvent> pending;
        pending.swap(g_QueuedScriptEvents);
        for (const QueuedScriptEvent& event : pending) {
            if (event.scene != scene) {
                g_QueuedScriptEvents.push_back(event);
                continue;
            }
            std::vector<std::function<bool(const std::string&, std::string&)>> callbacks;
            for (const auto& subscription : g_ScriptEventSubscriptions) {
                if (subscription.scene == scene && subscription.name == event.name) {
                    callbacks.push_back(subscription.callback);
                }
            }
            for (const auto& callback : callbacks) {
                if (!callback(event.payload, error)) return false;
            }
        }
        return true;
    }

    ScriptComponent& component;
    asIScriptEngine* engine = nullptr;
    asIScriptObject* object = nullptr;
    asITypeInfo* type = nullptr;
    asIScriptFunction* awake = nullptr;
    asIScriptFunction* onEnable = nullptr;
    asIScriptFunction* start = nullptr;
    asIScriptFunction* fixedUpdate = nullptr;
    asIScriptFunction* update = nullptr;
    asIScriptFunction* lateUpdate = nullptr;
    asIScriptFunction* onCollision = nullptr;
    asIScriptFunction* onAnimationEvent = nullptr;
    asIScriptFunction* onDisable = nullptr;
    asIScriptFunction* onDestroy = nullptr;
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json state = nlohmann::json::object();
    std::vector<ScriptFieldInfo> fields;
    std::ostringstream messages;
    struct Timer {
        uint64_t id = 0;
        float interval = 0.0f;
        float remaining = 0.0f;
        bool repeat = false;
        bool cancelled = false;
        asIScriptFunction* function = nullptr;
    };
    std::vector<Timer> timers;
    uint64_t nextTimerID = 1;
};

AngelScriptRuntime::AngelScriptRuntime(ScriptComponent& component)
    : m_Impl(std::make_unique<Impl>(component))
{
}

AngelScriptRuntime::~AngelScriptRuntime() = default;

bool AngelScriptRuntime::Load(const std::string& source, const std::string& chunkName,
                              const std::string& className,
                              const nlohmann::json& properties,
                              const nlohmann::json& state,
                              std::string& error)
{
    m_Impl->ClearUIEventSubscriptions();
    m_Impl->ClearScriptEventSubscriptions();
    m_Impl->CancelAllTimers();
    if (m_Impl->object) {
        m_Impl->object->Release();
        m_Impl->object = nullptr;
    }
    if (m_Impl->engine) {
        m_Impl->engine->ShutDownAndRelease();
        m_Impl->engine = nullptr;
    }
    m_Impl->type = nullptr;
    m_Impl->fields.clear();
    m_Impl->messages.str(std::string{});
    m_Impl->messages.clear();
    m_Impl->properties = properties.is_object() ? properties : nlohmann::json::object();
    m_Impl->state = state.is_object() ? state : nlohmann::json::object();

    m_Impl->engine = asCreateScriptEngine();
    if (!m_Impl->engine) {
        error = "failed to create AngelScript engine";
        return false;
    }
    m_Impl->engine->SetMessageCallback(
        asMETHOD(Impl, MessageCallback), m_Impl.get(), asCALL_THISCALL);
    RegisterScriptBindings(*m_Impl->engine);

    asIScriptModule* module = m_Impl->engine->GetModule("GameplayScript", asGM_ALWAYS_CREATE);
    if (!module) {
        error = "failed to create AngelScript module";
        return false;
    }
    int result = module->AddScriptSection(chunkName.c_str(), source.data(),
                                          static_cast<unsigned int>(source.size()));
    if (result < 0 || module->Build() < 0) {
        error = m_Impl->messages.str();
        if (error.empty()) error = "AngelScript compile failed";
        return false;
    }

    m_Impl->type = module->GetTypeInfoByName(className.c_str());
    if (!m_Impl->type) {
        error = "AngelScript class not found: " + className;
        return false;
    }
    m_Impl->object = static_cast<asIScriptObject*>(
        m_Impl->engine->CreateScriptObject(m_Impl->type));
    if (!m_Impl->object) {
        error = "failed to instantiate AngelScript class: " + className;
        return false;
    }

    m_Impl->fields = ReflectFields(*m_Impl->engine, *m_Impl->type, m_Impl->object);
    ApplyProperties(*m_Impl->object, *m_Impl->type, m_Impl->fields, m_Impl->properties);
    CaptureProperties(*m_Impl->object, *m_Impl->type, m_Impl->fields, m_Impl->properties);

    m_Impl->awake = m_Impl->type->GetMethodByDecl("void Awake()");
    m_Impl->onEnable = m_Impl->type->GetMethodByDecl("void OnEnable()");
    m_Impl->start = m_Impl->type->GetMethodByDecl("void Start()");
    m_Impl->fixedUpdate = m_Impl->type->GetMethodByDecl("void FixedUpdate(float)");
    m_Impl->update = m_Impl->type->GetMethodByDecl("void Update(float)");
    m_Impl->lateUpdate = m_Impl->type->GetMethodByDecl("void LateUpdate(float)");
    m_Impl->onCollision = m_Impl->type->GetMethodByDecl("void OnCollision(const CollisionEvent &in)");
    if (!m_Impl->onCollision) m_Impl->onCollision = m_Impl->type->GetMethodByDecl("void OnCollision(CollisionEvent)");
    m_Impl->onAnimationEvent = m_Impl->type->GetMethodByDecl("void OnAnimationEvent(const string &in, const string &in)");
    m_Impl->onDisable = m_Impl->type->GetMethodByDecl("void OnDisable()");
    m_Impl->onDestroy = m_Impl->type->GetMethodByDecl("void OnDestroy()");
    return true;
}

bool AngelScriptRuntime::Call(const char* methodDecl, float deltaSeconds, std::string& error)
{
    asIScriptFunction* function = nullptr;
    if (std::string(methodDecl) == "Awake") function = m_Impl->awake;
    else if (std::string(methodDecl) == "OnEnable") function = m_Impl->onEnable;
    else if (std::string(methodDecl) == "Start") function = m_Impl->start;
    else if (std::string(methodDecl) == "FixedUpdate") function = m_Impl->fixedUpdate;
    else if (std::string(methodDecl) == "Update") function = m_Impl->update;
    else if (std::string(methodDecl) == "LateUpdate") function = m_Impl->lateUpdate;
    else if (std::string(methodDecl) == "OnDisable") function = m_Impl->onDisable;
    else if (std::string(methodDecl) == "OnDestroy") function = m_Impl->onDestroy;
    return m_Impl->Execute(function, deltaSeconds,
                           function == m_Impl->fixedUpdate ||
                           function == m_Impl->update ||
                           function == m_Impl->lateUpdate,
                           nullptr, error);
}

bool AngelScriptRuntime::CallCollision(const CollisionEvent& event, std::string& error)
{
    const ScriptCollisionEvent scriptEvent = ConvertCollisionEvent(event);
    return m_Impl->Execute(m_Impl->onCollision, 0.0f, false, &scriptEvent, error);
}

bool AngelScriptRuntime::CallAnimationEvent(const std::string& name,
                                            const std::string& payload,
                                            std::string& error)
{
    return m_Impl->ExecuteAnimationEvent(m_Impl->onAnimationEvent, name, payload, error);
}

bool AngelScriptRuntime::SubscribeUIEvent(const std::string& elementId,
                                          const std::string& eventName,
                                          const std::string& callbackName,
                                          std::string& error)
{
    return m_Impl->SubscribeUIEvent(elementId, eventName, callbackName, error);
}

void AngelScriptRuntime::UnsubscribeUIEvent(const std::string& elementId,
                                            const std::string& eventName)
{
    m_Impl->UnsubscribeUIEvent(elementId, eventName);
}

void AngelScriptRuntime::ClearUIEventSubscriptions()
{
    m_Impl->ClearUIEventSubscriptions();
}

bool AngelScriptRuntime::SubscribeScriptEvent(const std::string& eventName,
                                              const std::string& callbackName,
                                              std::string& error)
{
    return m_Impl->SubscribeScriptEvent(eventName, callbackName, error);
}

bool AngelScriptRuntime::EmitScriptEvent(const std::string& eventName,
                                         const std::string& jsonPayload)
{
    return m_Impl->EmitScriptEvent(eventName, jsonPayload);
}

void AngelScriptRuntime::ClearScriptEventSubscriptions()
{
    m_Impl->ClearScriptEventSubscriptions();
}

uint64_t AngelScriptRuntime::ScheduleTimer(float seconds, bool repeat,
                                           const std::string& callbackName,
                                           std::string& error)
{
    return m_Impl->ScheduleTimer(seconds, repeat, callbackName, error);
}

void AngelScriptRuntime::CancelTimer(uint64_t timerID)
{
    m_Impl->CancelTimer(timerID);
}

void AngelScriptRuntime::CancelAllTimers()
{
    m_Impl->CancelAllTimers();
}

bool AngelScriptRuntime::TickServices(float deltaSeconds, std::string& error)
{
    return m_Impl->TickTimers(deltaSeconds, error) && m_Impl->FlushScriptEvents(error);
}

const nlohmann::json& AngelScriptRuntime::GetProperties() const
{
    if (m_Impl->object && m_Impl->type) {
        CaptureProperties(*m_Impl->object, *m_Impl->type, m_Impl->fields, m_Impl->properties);
    }
    return m_Impl->properties;
}

const nlohmann::json& AngelScriptRuntime::GetState() const
{
    return m_Impl->state;
}

const std::vector<ScriptFieldInfo>& AngelScriptRuntime::GetFields() const
{
    return m_Impl->fields;
}

void AngelScriptRuntime::SetUIEventBridge(UIEventBridge* bridge)
{
    Bindings().SetUIEventBridge(bridge);
}

void AngelScriptRuntime::ClearUIEventBridge(UIEventBridge* bridge)
{
    Bindings().ClearUIEventBridge(bridge);
}

void AngelScriptRuntime::SetUISystem(UISystem* system)
{
    Bindings().SetUISystem(system);
}

void AngelScriptRuntime::ClearUISystem(UISystem* system)
{
    Bindings().ClearUISystem(system);
}

std::vector<ScriptClassInfo> AngelScriptRuntime::DiscoverClasses(const std::string& source,
                                                                 const std::string& chunkName,
                                                                 std::string& error)
{
    error.clear();
    std::vector<ScriptClassInfo> classes;

    asIScriptEngine* engine = asCreateScriptEngine();
    if (!engine) {
        error = "failed to create AngelScript engine";
        return classes;
    }

    struct EngineGuard {
        asIScriptEngine* value = nullptr;
        ~EngineGuard() { if (value) value->ShutDownAndRelease(); }
    } guard{engine};

    std::ostringstream messages;
    struct Callback {
        static void Message(const asSMessageInfo* message, void* user)
        {
            auto* stream = static_cast<std::ostringstream*>(user);
            *stream << message->section << "(" << message->row << "," << message->col << ") ";
            if (message->type == asMSGTYPE_ERROR) *stream << "error: ";
            else if (message->type == asMSGTYPE_WARNING) *stream << "warning: ";
            else *stream << "info: ";
            *stream << message->message << "\n";
        }
    };
    engine->SetMessageCallback(asFUNCTION(Callback::Message), &messages, asCALL_CDECL);
    RegisterScriptBindings(*engine);

    asIScriptModule* module = engine->GetModule("GameplayScriptDiscovery", asGM_ALWAYS_CREATE);
    if (!module) {
        error = "failed to create AngelScript module";
        return classes;
    }

    const int addResult = module->AddScriptSection(chunkName.c_str(), source.data(),
                                                  static_cast<unsigned int>(source.size()));
    if (addResult < 0 || module->Build() < 0) {
        error = messages.str();
        if (error.empty()) error = "AngelScript compile failed";
        return classes;
    }

    const asUINT typeCount = module->GetObjectTypeCount();
    for (asUINT i = 0; i < typeCount; ++i) {
        asITypeInfo* type = module->GetObjectTypeByIndex(i);
        if (!type || !(type->GetFlags() & asOBJ_SCRIPT_OBJECT)) continue;

        ScriptClassInfo info;
        info.name = type->GetName() ? type->GetName() : "";
        asIScriptObject* object = static_cast<asIScriptObject*>(engine->CreateScriptObject(type));
        if (object) {
            info.fields = ReflectFields(*engine, *type, object);
            object->Release();
        } else {
            info.fields = ReflectFields(*engine, *type, nullptr);
        }
        if (!info.name.empty()) classes.push_back(std::move(info));
    }
    return classes;
}

bool AngelScriptRuntime::PreprocessSource(const std::string& source,
                                          const std::string& chunkName,
                                          std::string& outSource,
                                          std::vector<std::string>* outDependencies,
                                          std::string& error)
{
    outSource.clear();
    if (outDependencies) outDependencies->clear();
    error.clear();
    std::filesystem::path baseDirectory;
    if (!chunkName.empty() && chunkName != "InlineScript") {
        const std::filesystem::path chunkPath(chunkName);
        if (chunkPath.has_parent_path()) baseDirectory = chunkPath.parent_path();
    }
    std::unordered_set<std::string> includeStack;
    return PreprocessScriptSourceRecursive(source, chunkName.empty() ? "InlineScript" : chunkName,
                                           baseDirectory, outSource, outDependencies,
                                           includeStack, error, 0);
}
