#include "Scene/ComponentRegistry.h"
#include "Scene/TypeRegistry.h"

#include "Audio/AudioSourceComponent.h"
#include "Audio/AudioListenerComponent.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Animation/AnimatorComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/ThirdPersonCameraComponent.h"
#include "Gameplay/GameplayComponents.h"
#include "Gameplay/EnemyAIComponent.h"
#include "Navigation/NavAgentComponent.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/ProbeComponents.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ParticleSystemComponent.h"
#include "Renderer/SkylightComponent.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scripting/ScriptComponent.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#include <algorithm>
#include <cctype>

namespace {
PropertyFlags EditableFlags() {
    return PropertyFlags::Serialize | PropertyFlags::Inspector | PropertyFlags::ScriptRead |
           PropertyFlags::ScriptWrite | PropertyFlags::PrefabOverride;
}
nlohmann::json Vec3Json(const Vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}
bool ReadVec3(const nlohmann::json& value, Vec3& out, std::string* error) {
    if (!value.is_array() || value.size() != 3) {
        if (error)
            *error = "expected Vec3 array";
        return false;
    }
    out = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return true;
}
bool IdentityMigration(nlohmann::json&, std::string*) {
    return true;
}
template <typename T> TypeDescriptor BeginType(const char* name, const char* display, const char* category) {
    TypeDescriptor d;
    d.stableName = name;
    d.displayName = display;
    d.category = category;
    d.schemaVersion = 1;
    d.cppType = std::type_index(typeid(T));
    d.factory = [] { return std::make_unique<T>(); };
    d.metadataComplete = true;
    d.customInspector = [](void*) {};
    d.migrations.emplace(0, IdentityMigration);
    return d;
}
void AddBool(TypeDescriptor& d, const char* name, bool defaultValue, std::function<bool(const Component&)> get,
             std::function<void(Component&, bool)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::Bool;
    p.flags = EditableFlags();
    p.defaultValue = defaultValue;
    p.getter = [get](const Component& c) { return nlohmann::json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string*) {
        set(c, v.get<bool>());
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddFloat(TypeDescriptor& d, const char* name, float defaultValue, std::function<float(const Component&)> get,
              std::function<void(Component&, float)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::Float;
    p.flags = EditableFlags();
    p.defaultValue = defaultValue;
    p.getter = [get](const Component& c) { return nlohmann::json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string*) {
        set(c, v.get<float>());
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddUInt(TypeDescriptor& d, const char* name, uint32_t defaultValue, std::function<uint32_t(const Component&)> get,
             std::function<void(Component&, uint32_t)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::UInt32;
    p.flags = EditableFlags();
    p.defaultValue = defaultValue;
    p.getter = [get](const Component& c) { return nlohmann::json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string*) {
        set(c, v.get<uint32_t>());
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddString(TypeDescriptor& d, const char* name, const char* defaultValue, PropertyKind kind,
               std::function<std::string(const Component&)> get,
               std::function<void(Component&, const std::string&)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = kind;
    p.flags = EditableFlags();
    p.defaultValue = defaultValue;
    p.getter = [get](const Component& c) { return nlohmann::json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string*) {
        set(c, v.get<std::string>());
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddEnum(TypeDescriptor& d, const char* name, int defaultValue, std::function<int(const Component&)> get,
             std::function<void(Component&, int)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::Enum;
    p.flags = EditableFlags();
    p.defaultValue = defaultValue;
    p.getter = [get](const Component& c) { return nlohmann::json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string*) {
        set(c, v.get<int>());
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddVec3(TypeDescriptor& d, const char* name, const Vec3& defaultValue, std::function<Vec3(const Component&)> get,
             std::function<void(Component&, const Vec3&)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::Vec3;
    p.flags = EditableFlags();
    p.defaultValue = Vec3Json(defaultValue);
    p.getter = [get](const Component& c) { return Vec3Json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string* e) {
        Vec3 value;
        if (!ReadVec3(v, value, e))
            return false;
        set(c, value);
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddColor(TypeDescriptor& d, const char* name, const Vec3& defaultValue, std::function<Vec3(const Component&)> get,
              std::function<void(Component&, const Vec3&)> set) {
    PropertyDescriptor p;
    p.stableName = name;
    p.kind = PropertyKind::Color;
    p.flags = EditableFlags();
    p.defaultValue = Vec3Json(defaultValue);
    p.getter = [get](const Component& c) { return Vec3Json(get(c)); };
    p.setter = [set](Component& c, const nlohmann::json& v, std::string* e) {
        Vec3 value;
        if (!ReadVec3(v, value, e))
            return false;
        set(c, value);
        return true;
    };
    d.properties.push_back(std::move(p));
}
void AddColliderProperties(TypeDescriptor& d) {
    AddBool(
        d, "isTrigger", false, [](const Component& c) { return static_cast<const ColliderComponent&>(c).IsTrigger(); },
        [](Component& c, bool v) { static_cast<ColliderComponent&>(c).SetTrigger(v); });
    AddUInt(
        d, "layer", 1, [](const Component& c) { return static_cast<const ColliderComponent&>(c).GetLayer(); },
        [](Component& c, uint32_t v) { static_cast<ColliderComponent&>(c).SetLayer(v); });
    AddUInt(
        d, "layerMask", ~uint32_t{0},
        [](const Component& c) { return static_cast<const ColliderComponent&>(c).GetLayerMask(); },
        [](Component& c, uint32_t v) { static_cast<ColliderComponent&>(c).SetLayerMask(v); });
}

PropertyKind InferPropertyKind(const std::string& name, const nlohmann::json& value) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.is_boolean())
        return PropertyKind::Bool;
    if (value.is_number_float())
        return PropertyKind::Float;
    if (value.is_number_unsigned())
        return PropertyKind::UInt64;
    if (value.is_number_integer())
        return PropertyKind::Int32;
    if (value.is_string())
        return lower.find("path") != std::string::npos || lower.find("asset") != std::string::npos || lower == "mesh" ||
                       lower == "material" || lower == "clip"
                   ? PropertyKind::AssetPath
                   : PropertyKind::String;
    if (value.is_array()) {
        if (value.size() == 2)
            return PropertyKind::Vec2;
        if (value.size() == 3)
            return lower.find("color") != std::string::npos ? PropertyKind::Color : PropertyKind::Vec3;
        if (value.size() == 4)
            return lower.find("color") != std::string::npos ? PropertyKind::Color : PropertyKind::Vec4;
    }
    return PropertyKind::Json;
}
template <typename T> TypeDescriptor ReflectedLegacyType(const char* name, const char* display, const char* category) {
    auto d = BeginType<T>(name, display, category);
    auto defaults = std::make_unique<T>();
    nlohmann::json schema = nlohmann::json::object();
    defaults->Serialize(schema);
    d.legacyDeserialize = [](Component& component, const nlohmann::json& value) { component.Deserialize(value); };
    if (schema.is_object())
        for (auto it = schema.begin(); it != schema.end(); ++it) {
            PropertyDescriptor p;
            p.stableName = it.key();
            p.kind = InferPropertyKind(p.stableName, it.value());
            p.flags = EditableFlags();
            p.defaultValue = it.value();
            const std::string key = p.stableName;
            p.getter = [key](const Component& component) {
                nlohmann::json value = nlohmann::json::object();
                component.Serialize(value);
                auto found = value.find(key);
                return found == value.end() ? nlohmann::json() : *found;
            };
            p.setter = [key](Component& component, const nlohmann::json& input, std::string* error) {
                try {
                    nlohmann::json value = nlohmann::json::object();
                    component.Serialize(value);
                    value[key] = input;
                    component.Deserialize(value);
                    return true;
                } catch (const std::exception& e) {
                    if (error)
                        *error = e.what();
                    return false;
                }
            };
            d.properties.push_back(std::move(p));
        }
    return d;
}

TypeDescriptor RigidBodyType() {
    auto d = BeginType<RigidBodyComponent>("RigidBody", "Rigid Body", "Physics");
    AddEnum(
        d, "bodyType", static_cast<int>(BodyType::Dynamic),
        [](const Component& c) { return static_cast<int>(static_cast<const RigidBodyComponent&>(c).GetBodyType()); },
        [](Component& c, int v) { static_cast<RigidBodyComponent&>(c).SetBodyType(static_cast<BodyType>(v)); });
    AddFloat(
        d, "mass", 1.0f, [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetMass(); },
        [](Component& c, float v) { static_cast<RigidBodyComponent&>(c).SetMass(v); });
    AddVec3(
        d, "velocity", Vec3::Zero(),
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetVelocity(); },
        [](Component& c, const Vec3& v) { static_cast<RigidBodyComponent&>(c).SetVelocity(v); });
    AddVec3(
        d, "angularVelocity", Vec3::Zero(),
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetAngularVelocity(); },
        [](Component& c, const Vec3& v) { static_cast<RigidBodyComponent&>(c).SetAngularVelocity(v); });
    AddFloat(
        d, "restitution", 0.1f,
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetRestitution(); },
        [](Component& c, float v) { static_cast<RigidBodyComponent&>(c).SetRestitution(v); });
    AddFloat(
        d, "linearDamping", 0.05f,
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetLinearDamping(); },
        [](Component& c, float v) { static_cast<RigidBodyComponent&>(c).SetLinearDamping(v); });
    AddFloat(
        d, "angularDamping", 0.05f,
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetAngularDamping(); },
        [](Component& c, float v) { static_cast<RigidBodyComponent&>(c).SetAngularDamping(v); });
    AddFloat(
        d, "friction", 0.6f, [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetFriction(); },
        [](Component& c, float v) { static_cast<RigidBodyComponent&>(c).SetFriction(v); });
    AddBool(
        d, "useGravity", true,
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).UsesGravity(); },
        [](Component& c, bool v) { static_cast<RigidBodyComponent&>(c).SetUseGravity(v); });
    AddVec3(
        d, "linearAxisLocks", Vec3::Zero(),
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetLinearAxisLocks(); },
        [](Component& c, const Vec3& v) { static_cast<RigidBodyComponent&>(c).SetLinearAxisLocks(v); });
    AddVec3(
        d, "angularAxisLocks", Vec3::Zero(),
        [](const Component& c) { return static_cast<const RigidBodyComponent&>(c).GetAngularAxisLocks(); },
        [](Component& c, const Vec3& v) { static_cast<RigidBodyComponent&>(c).SetAngularAxisLocks(v); });
    AddEnum(
        d, "collisionDetection", static_cast<int>(CollisionDetectionMode::Discrete),
        [](const Component& c) {
            return static_cast<int>(static_cast<const RigidBodyComponent&>(c).GetCollisionDetectionMode());
        },
        [](Component& c, int v) {
            static_cast<RigidBodyComponent&>(c).SetCollisionDetectionMode(static_cast<CollisionDetectionMode>(v));
        });
    return d;
}
TypeDescriptor SphereColliderType() {
    auto d = BeginType<SphereColliderComponent>("SphereCollider", "Sphere Collider", "Physics");
    AddColliderProperties(d);
    AddFloat(
        d, "radius", 0.5f,
        [](const Component& c) { return static_cast<const SphereColliderComponent&>(c).GetRadius(); },
        [](Component& c, float v) { static_cast<SphereColliderComponent&>(c).SetRadius(v); });
    return d;
}
TypeDescriptor CapsuleColliderType() {
    auto d = BeginType<CapsuleColliderComponent>("CapsuleCollider", "Capsule Collider", "Physics");
    AddColliderProperties(d);
    AddFloat(
        d, "radius", 0.5f,
        [](const Component& c) { return static_cast<const CapsuleColliderComponent&>(c).GetRadius(); },
        [](Component& c, float v) { static_cast<CapsuleColliderComponent&>(c).SetRadius(v); });
    AddFloat(
        d, "halfHeight", 0.5f,
        [](const Component& c) { return static_cast<const CapsuleColliderComponent&>(c).GetHalfHeight(); },
        [](Component& c, float v) { static_cast<CapsuleColliderComponent&>(c).SetHalfHeight(v); });
    return d;
}
TypeDescriptor CharacterControllerType() {
    auto d = BeginType<CharacterControllerComponent>("CharacterController", "Character Controller", "Physics");
    AddBool(
        d, "useGravity", true,
        [](const Component& c) { return static_cast<const CharacterControllerComponent&>(c).UsesGravity(); },
        [](Component& c, bool v) { static_cast<CharacterControllerComponent&>(c).SetUseGravity(v); });
    AddFloat(
        d, "stepOffset", 0.3f,
        [](const Component& c) { return static_cast<const CharacterControllerComponent&>(c).GetStepOffset(); },
        [](Component& c, float v) { static_cast<CharacterControllerComponent&>(c).SetStepOffset(v); });
    AddFloat(
        d, "maxSlopeAngle", 50.0f,
        [](const Component& c) { return static_cast<const CharacterControllerComponent&>(c).GetMaxSlopeAngle(); },
        [](Component& c, float v) { static_cast<CharacterControllerComponent&>(c).SetMaxSlopeAngle(v); });
    AddFloat(
        d, "jumpSpeed", 5.5f,
        [](const Component& c) { return static_cast<const CharacterControllerComponent&>(c).GetJumpSpeed(); },
        [](Component& c, float v) { static_cast<CharacterControllerComponent&>(c).SetJumpSpeed(v); });
    AddFloat(
        d, "airControl", 0.35f,
        [](const Component& c) { return static_cast<const CharacterControllerComponent&>(c).GetAirControl(); },
        [](Component& c, float v) { static_cast<CharacterControllerComponent&>(c).SetAirControl(v); });
    return d;
}
TypeDescriptor PostProcessType() {
    auto d = BeginType<PostProcessComponent>("PostProcess", "Post Process", "Rendering");
    AddBool(
        d, "toneMapping", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsToneMappingEnabled(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetToneMappingEnabled(v); });
    AddFloat(
        d, "exposure", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetExposure(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetExposure(v); });
    AddFloat(
        d, "gamma", 2.2f, [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetGamma(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetGamma(v); });
    AddFloat(
        d, "vignette", 0.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetVignette(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetVignette(v); });
    AddFloat(
        d, "saturation", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSaturation(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSaturation(v); });
    AddFloat(
        d, "contrast", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetContrast(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetContrast(v); });
    AddFloat(
        d, "antiAliasingStrength", 0.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetAntiAliasingStrength(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetAntiAliasingStrength(v); });
    AddBool(
        d, "bloomEnabled", false,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsBloomEnabled(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetBloomEnabled(v); });
    AddFloat(
        d, "bloomThreshold", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetBloomThreshold(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetBloomThreshold(v); });
    AddFloat(
        d, "bloomIntensity", 0.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetBloomIntensity(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetBloomIntensity(v); });
    AddFloat(
        d, "ssaoRadius", 1.2f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSAORadius(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSAORadius(v); });
    AddUInt(
        d, "ssaoSampleCount", 16u,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSAOSampleCount(); },
        [](Component& c, uint32_t v) { static_cast<PostProcessComponent&>(c).SetSSAOSampleCount(v); });
    AddFloat(
        d, "ssaoBias", 0.025f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSAOBias(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSAOBias(v); });
    AddFloat(
        d, "ssaoPower", 1.5f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSAOPower(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSAOPower(v); });
    AddFloat(
        d, "ssaoIntensity", 0.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSAOIntensity(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSAOIntensity(v); });
    AddBool(
        d, "ssaoHalfResolution", false,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsSSAOHalfResolution(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetSSAOHalfResolution(v); });
    AddBool(
        d, "rayTracedAOReplacement", false,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).UsesRayTracedAOReplacement(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetRayTracedAOReplacement(v); });
    AddBool(
        d, "ssgiEnabled", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsSSGIEnabled(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetSSGIEnabled(v); });
    AddBool(
        d, "ssgiHalfResolution", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsSSGIHalfResolution(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetSSGIHalfResolution(v); });
    AddFloat(
        d, "ssgiIntensity", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSGIIntensity(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSGIIntensity(v); });
    AddFloat(
        d, "ssgiMaxDistance", 10.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSGIMaxDistance(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSGIMaxDistance(v); });
    AddFloat(
        d, "ssgiHistoryWeight", 0.9f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSGIHistoryWeight(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSGIHistoryWeight(v); });
    AddUInt(
        d, "ssgiStepCount", 32u,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSGIStepCount(); },
        [](Component& c, uint32_t v) { static_cast<PostProcessComponent&>(c).SetSSGIStepCount(v); });
    AddUInt(
        d, "ssgiFilterRounds", 3u,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSGIFilterRounds(); },
        [](Component& c, uint32_t v) { static_cast<PostProcessComponent&>(c).SetSSGIFilterRounds(v); });
    AddBool(
        d, "rayTracedDiffuseReplacement", false,
        [](const Component& c) {
            return static_cast<const PostProcessComponent&>(c).UsesRayTracedDiffuseReplacement();
        },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetRayTracedDiffuseReplacement(v); });
    AddBool(
        d, "ssrEnabled", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsSSREnabled(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetSSREnabled(v); });
    AddBool(
        d, "ssrHalfResolution", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsSSRHalfResolution(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetSSRHalfResolution(v); });
    AddFloat(
        d, "ssrMaxDistance", 10.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSRMaxDistance(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSRMaxDistance(v); });
    AddFloat(
        d, "ssrMaxRoughness", 0.8f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSRMaxRoughness(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSRMaxRoughness(v); });
    AddFloat(
        d, "ssrHistoryWeight", 0.9f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSRHistoryWeight(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetSSRHistoryWeight(v); });
    AddUInt(
        d, "ssrStepCount", 48u,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSRStepCount(); },
        [](Component& c, uint32_t v) { static_cast<PostProcessComponent&>(c).SetSSRStepCount(v); });
    AddUInt(
        d, "ssrFilterRounds", 2u,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetSSRFilterRounds(); },
        [](Component& c, uint32_t v) { static_cast<PostProcessComponent&>(c).SetSSRFilterRounds(v); });
    AddBool(
        d, "rayTracedReflectionReplacement", false,
        [](const Component& c) {
            return static_cast<const PostProcessComponent&>(c).UsesRayTracedReflectionReplacement();
        },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetRayTracedReflectionReplacement(v); });
    AddBool(
        d, "rayTracedShadowReplacement", false,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).UsesRayTracedShadowReplacement(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetRayTracedShadowReplacement(v); });
    AddBool(
        d, "taaEnabled", true,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).IsTAAEnabled(); },
        [](Component& c, bool v) { static_cast<PostProcessComponent&>(c).SetTAAEnabled(v); });
    AddFloat(
        d, "taaHistoryWeight", 0.8f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetTAAHistoryWeight(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetTAAHistoryWeight(v); });
    AddFloat(
        d, "taaJitterSpread", 1.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetTAAJitterSpread(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetTAAJitterSpread(v); });
    AddFloat(
        d, "taaHistoryClipExpansion", 0.0f,
        [](const Component& c) { return static_cast<const PostProcessComponent&>(c).GetTAAHistoryClipExpansion(); },
        [](Component& c, float v) { static_cast<PostProcessComponent&>(c).SetTAAHistoryClipExpansion(v); });
    return d;
}
TypeDescriptor AudioSourceType() {
    auto d = ReflectedLegacyType<AudioSourceComponent>("AudioSource", "Audio Source", "Audio");
    AddString(
        d, "clip", "", PropertyKind::AssetPath,
        [](const Component& c) { return static_cast<const AudioSourceComponent&>(c).GetClipPath(); },
        [](Component& c, const std::string& v) { static_cast<AudioSourceComponent&>(c).SetClipPath(v); });
    return d;
}

TypeDescriptor CameraType() {
    MYENGINE_BEGIN_COMPONENT_TYPE(CameraComponent, "Camera", 1)
    _myengine_type_descriptor.displayName = "Camera";
    _myengine_type_descriptor.category = "Rendering";
    _myengine_type_descriptor.customInspector = [](void*) {};
    PropertyHints fov;
    fov.displayName = "Field of View";
    fov.hasRange = true;
    fov.minimum = 1;
    fov.maximum = 179;
    fov.step = 1;
    MYENGINE_PROPERTY_ACCESSOR(
        "mainCamera", PropertyKind::Bool,
        [](const Component& c) { return nlohmann::json(static_cast<const CameraComponent&>(c).IsMainCamera()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<CameraComponent&>(c).SetMainCamera(v.get<bool>());
            return true;
        },
        EditableFlags(), true, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "fovYDegrees", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const CameraComponent&>(c).GetFovYDegrees()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<CameraComponent&>(c).SetFovYDegrees(v.get<float>());
            return true;
        },
        EditableFlags(), 60.0f, fov);
    MYENGINE_PROPERTY_ACCESSOR(
        "nearClip", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const CameraComponent&>(c).GetNearClip()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<CameraComponent&>(c).SetNearClip(v.get<float>());
            return true;
        },
        EditableFlags(), 0.1f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "farClip", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const CameraComponent&>(c).GetFarClip()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<CameraComponent&>(c).SetFarClip(v.get<float>());
            return true;
        },
        EditableFlags(), 1000.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "clearColor", PropertyKind::Color,
        [](const Component& c) { return Vec3Json(static_cast<const CameraComponent&>(c).GetClearColor()); },
        [](Component& c, const nlohmann::json& v, std::string* e) {
            Vec3 x;
            if (!ReadVec3(v, x, e))
                return false;
            static_cast<CameraComponent&>(c).SetClearColor(x);
            return true;
        },
        EditableFlags(), Vec3Json({0.12f, 0.12f, 0.18f}), PropertyHints{});
    MYENGINE_MIGRATION(0, IdentityMigration);
    return MYENGINE_END_COMPONENT_TYPE();
}
TypeDescriptor LightTypeDescriptor() {
    MYENGINE_BEGIN_COMPONENT_TYPE(LightComponent, "Light", 1)
    _myengine_type_descriptor.displayName = "Light";
    _myengine_type_descriptor.category = "Rendering";
    _myengine_type_descriptor.customInspector = [](void*) {};
    MYENGINE_PROPERTY_ACCESSOR(
        "type", PropertyKind::Enum,
        [](const Component& c) {
            return nlohmann::json(static_cast<int>(static_cast<const LightComponent&>(c).GetLightType()));
        },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetLightType(static_cast<LightType>(v.get<int>()));
            return true;
        },
        EditableFlags(), 0, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "color", PropertyKind::Color,
        [](const Component& c) { return Vec3Json(static_cast<const LightComponent&>(c).GetColor()); },
        [](Component& c, const nlohmann::json& v, std::string* e) {
            Vec3 x;
            if (!ReadVec3(v, x, e))
                return false;
            static_cast<LightComponent&>(c).SetColor(x);
            return true;
        },
        EditableFlags(), Vec3Json(Vec3::One()), PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "intensity", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).GetIntensity()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetIntensity(v.get<float>());
            return true;
        },
        EditableFlags(), 3.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "range", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).GetRange()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetRange(v.get<float>());
            return true;
        },
        EditableFlags(), 8.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "innerConeAngle", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).GetInnerConeAngle()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetInnerConeAngle(v.get<float>());
            return true;
        },
        EditableFlags(), 25.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "outerConeAngle", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).GetOuterConeAngle()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetOuterConeAngle(v.get<float>());
            return true;
        },
        EditableFlags(), 35.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "shadowIntensity", PropertyKind::Float,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).GetShadowIntensity()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetShadowIntensity(v.get<float>());
            return true;
        },
        EditableFlags(), 1.0f, PropertyHints{});
    MYENGINE_PROPERTY_ACCESSOR(
        "castsShadows", PropertyKind::Bool,
        [](const Component& c) { return nlohmann::json(static_cast<const LightComponent&>(c).CastsShadows()); },
        [](Component& c, const nlohmann::json& v, std::string*) {
            static_cast<LightComponent&>(c).SetCastShadows(v.get<bool>());
            return true;
        },
        EditableFlags(), true, PropertyHints{});
    MYENGINE_MIGRATION(0, IdentityMigration);
    return MYENGINE_END_COMPONENT_TYPE();
}
TypeDescriptor SkylightTypeDescriptor() {
    auto d = BeginType<SkylightComponent>("Skylight", "Skylight", "Rendering");
    AddColor(
        d, "environmentColor", Vec3::One(),
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetEnvironmentColor(); },
        [](Component& c, const Vec3& v) { static_cast<SkylightComponent&>(c).SetEnvironmentColor(v); });
    AddFloat(
        d, "environmentIntensity", 1.0f,
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetEnvironmentIntensity(); },
        [](Component& c, float v) { static_cast<SkylightComponent&>(c).SetEnvironmentIntensity(v); });
    AddFloat(
        d, "skyIntensity", 1.0f,
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetSkyIntensity(); },
        [](Component& c, float v) { static_cast<SkylightComponent&>(c).SetSkyIntensity(v); });
    AddColor(
        d, "skyTint", Vec3::One(),
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetSkyTint(); },
        [](Component& c, const Vec3& v) { static_cast<SkylightComponent&>(c).SetSkyTint(v); });
    AddColor(
        d, "horizonTint", Vec3::One(),
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetHorizonTint(); },
        [](Component& c, const Vec3& v) { static_cast<SkylightComponent&>(c).SetHorizonTint(v); });
    AddColor(
        d, "groundTint", Vec3::One(),
        [](const Component& c) { return static_cast<const SkylightComponent&>(c).GetGroundTint(); },
        [](Component& c, const Vec3& v) { static_cast<SkylightComponent&>(c).SetGroundTint(v); });
    return d;
}
TypeDescriptor BoxColliderType() {
    MYENGINE_BEGIN_COMPONENT_TYPE(BoxColliderComponent, "BoxCollider", 1)
    _myengine_type_descriptor.displayName = "Box Collider";
    _myengine_type_descriptor.category = "Physics";
    _myengine_type_descriptor.customInspector = [](void*) {};
    AddColliderProperties(_myengine_type_descriptor);
    MYENGINE_PROPERTY_ACCESSOR(
        "halfExtents", PropertyKind::Vec3,
        [](const Component& c) { return Vec3Json(static_cast<const BoxColliderComponent&>(c).GetHalfExtents()); },
        [](Component& c, const nlohmann::json& v, std::string* e) {
            Vec3 x;
            if (!ReadVec3(v, x, e))
                return false;
            static_cast<BoxColliderComponent&>(c).SetHalfExtents(x);
            return true;
        },
        EditableFlags(), Vec3Json(Vec3(0.5f)), PropertyHints{});
    MYENGINE_MIGRATION(0, IdentityMigration);
    return MYENGINE_END_COMPONENT_TYPE();
}
} // namespace

ComponentRegistry& ComponentRegistry::Get() {
    static ComponentRegistry registry;
    return registry;
}

ComponentRegistry::ComponentRegistry() {
    TypeRegistry::Get().Register(
        ReflectedLegacyType<MeshRendererComponent>("MeshRenderer", "Mesh Renderer", "Rendering"));
    TypeRegistry::Get().Register(CameraType());
    TypeRegistry::Get().Register(
        ReflectedLegacyType<SkinnedMeshRendererComponent>("SkinnedMeshRenderer", "Skinned Mesh Renderer", "Animation"));
    TypeRegistry::Get().Register(ReflectedLegacyType<AnimatorComponent>("Animator", "Animator", "Animation"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<ThirdPersonCameraComponent>("ThirdPersonCamera", "Third Person Camera", "Camera"));
    TypeRegistry::Get().Register(ReflectedLegacyType<HealthComponent>("Health", "Health", "Gameplay"));
    TypeRegistry::Get().Register(ReflectedLegacyType<HurtboxComponent>("Hurtbox", "Hurtbox", "Gameplay"));
    TypeRegistry::Get().Register(ReflectedLegacyType<HitboxComponent>("Hitbox", "Hitbox", "Gameplay"));
    TypeRegistry::Get().Register(ReflectedLegacyType<InteractionComponent>("Interaction", "Interaction", "Gameplay"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<GameplayFeedbackComponent>("GameplayFeedback", "Gameplay Feedback", "Gameplay"));
    TypeRegistry::Get().Register(ReflectedLegacyType<NavAgentComponent>("NavAgent", "Navigation Agent", "Navigation"));
    TypeRegistry::Get().Register(ReflectedLegacyType<EnemyAIComponent>("EnemyAI", "Enemy AI", "Gameplay"));
    TypeRegistry::Get().Register(ReflectedLegacyType<ScriptComponent>("Script", "Script", "Scripting"));
    TypeRegistry::Get().Register(RigidBodyType());
    TypeRegistry::Get().Register(BoxColliderType());
    TypeRegistry::Get().Register(SphereColliderType());
    TypeRegistry::Get().Register(CapsuleColliderType());
    TypeRegistry::Get().Register(CharacterControllerType());
    TypeRegistry::Get().Register(LightTypeDescriptor());
    TypeRegistry::Get().Register(SkylightTypeDescriptor());
    TypeRegistry::Get().Register(
        ReflectedLegacyType<ReflectionProbeComponent>("ReflectionProbe", "Reflection Probe", "Rendering"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<SHProbeVolumeComponent>("SHProbeVolume", "SH Probe Volume", "Rendering"));
    TypeRegistry::Get().Register(PostProcessType());
    TypeRegistry::Get().Register(AudioSourceType());
    TypeRegistry::Get().Register(
        ReflectedLegacyType<AudioListenerComponent>("AudioListener", "Audio Listener", "Audio"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<ParticleSystemComponent>("ParticleSystem", "Particle System", "Rendering"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UICanvasComponent>("UICanvas", "UI Canvas", "UI"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<UIRectTransformComponent>("UIRectTransform", "UI Rect Transform", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UITextComponent>("UIText", "UI Text", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UIImageComponent>("UIImage", "UI Image", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UIButtonComponent>("UIButton", "UI Button", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UISliderComponent>("UISlider", "UI Slider", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UIProgressBarComponent>("UIProgressBar", "UI Progress Bar", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UIScrollViewComponent>("UIScrollView", "UI Scroll View", "UI"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<UIVerticalLayoutComponent>("UIVerticalLayout", "UI Vertical Layout", "UI"));
    TypeRegistry::Get().Register(
        ReflectedLegacyType<UIHorizontalLayoutComponent>("UIHorizontalLayout", "UI Horizontal Layout", "UI"));
    TypeRegistry::Get().Register(ReflectedLegacyType<UIGridLayoutComponent>("UIGridLayout", "UI Grid Layout", "UI"));
}

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
