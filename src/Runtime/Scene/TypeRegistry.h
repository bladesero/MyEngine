#pragma once

#include "API/RuntimeApi.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

class Component;

using TypeId = uint64_t;
using PropertyId = uint64_t;

enum class PropertyKind {
    Bool,
    Int32,
    UInt32,
    UInt64,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Enum,
    AssetPath,
    ActorHandle,
    Json
};
enum class PropertyFlags : uint32_t {
    None = 0,
    Serialize = 1u << 0,
    Inspector = 1u << 1,
    ScriptRead = 1u << 2,
    ScriptWrite = 1u << 3,
    PrefabOverride = 1u << 4,
    Transient = 1u << 5,
    ReadOnly = 1u << 6
};
inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b) {
    return static_cast<PropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool HasPropertyFlag(PropertyFlags value, PropertyFlags flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

struct PropertyHints {
    std::string displayName;
    std::string group;
    int order = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
    bool hasRange = false;
    std::vector<std::string> enumItems;
    std::string assetType;
};

struct PropertyDescriptor {
    std::string stableName;
    PropertyId id = 0;
    PropertyKind kind = PropertyKind::Json;
    PropertyFlags flags = PropertyFlags::None;
    nlohmann::json defaultValue;
    PropertyHints hints;
    std::vector<std::string> aliases;
    std::string scriptName;
    std::string scriptType;
    std::function<nlohmann::json(const Component&)> getter;
    std::function<bool(Component&, const nlohmann::json&, std::string*)> setter;
};

struct TypeDescriptor {
    using Factory = std::function<std::unique_ptr<Component>()>;
    std::string stableName;
    std::string displayName;
    std::string category;
    TypeId id = 0;
    uint32_t schemaVersion = 0;
    int menuOrder = 0;
    int executionOrder = 0;
    bool metadataComplete = false;
    std::type_index cppType{typeid(void)};
    Factory factory;
    std::vector<PropertyDescriptor> properties;
    std::unordered_map<uint32_t, std::function<bool(nlohmann::json&, std::string*)>> migrations;
    std::function<void(Component&, nlohmann::json&)> legacySerialize;
    std::function<void(Component&, const nlohmann::json&)> legacyDeserialize;
    std::function<void(void*)> customInspector;
    std::function<void(void*)> scriptBinder;
};

class MYENGINE_RUNTIME_API TypeRegistry {
public:
    static TypeRegistry& Get();
    static TypeId StableId(const std::string& text);
    bool Register(TypeDescriptor descriptor, std::string* error = nullptr);
    bool Freeze(std::string* error = nullptr);
    bool IsFrozen() const { return m_Frozen; }
    const TypeDescriptor* Find(const std::string& stableName) const;
    const TypeDescriptor* Find(TypeId id) const;
    const PropertyDescriptor* FindProperty(const TypeDescriptor& type, const std::string& nameOrAlias) const;
    std::unique_ptr<Component> Create(const std::string& stableName) const;
    std::vector<std::string> GetRegisteredTypes() const;
    std::vector<std::string> GetIncompleteMetadataTypes() const;
    bool Serialize(const Component& component, nlohmann::json& data, uint32_t& version,
                   std::string* error = nullptr) const;
    bool Deserialize(Component& component, const std::string& stableName, uint32_t version, const nlohmann::json& data,
                     std::string* error = nullptr) const;
    bool GetProperty(const Component& component, const std::string& property, nlohmann::json& value) const;
    bool SetProperty(Component& component, const std::string& property, const nlohmann::json& value,
                     std::string* error = nullptr) const;

private:
    std::unordered_map<std::string, TypeDescriptor> m_Types;
    std::unordered_map<TypeId, std::string> m_TypeIds;
    bool m_Frozen = false;
};

// Registration macros deliberately only build descriptors. Runtime behavior remains in TypeRegistry.
#define MYENGINE_BEGIN_COMPONENT_TYPE(Type, StableName, Version)                                                       \
    TypeDescriptor _myengine_type_descriptor;                                                                          \
    using _MyEngineRegisteredType = Type;                                                                              \
    _myengine_type_descriptor.stableName = StableName;                                                                 \
    _myengine_type_descriptor.schemaVersion = Version;                                                                 \
    _myengine_type_descriptor.cppType = std::type_index(typeid(Type));                                                 \
    _myengine_type_descriptor.metadataComplete = true;                                                                 \
    _myengine_type_descriptor.factory = [] { return std::make_unique<Type>(); };
#define MYENGINE_PROPERTY(Member, StableName, Flags, DefaultValue, Hints)                                              \
    static_assert(sizeof(Member) == 0, "Use MYENGINE_PROPERTY_ACCESSOR for private component state")
#define MYENGINE_PROPERTY_ACCESSOR(StableName, Kind, Getter, Setter, Flags, DefaultValue, Hints)                       \
    do {                                                                                                               \
        PropertyDescriptor _p;                                                                                         \
        _p.stableName = StableName;                                                                                    \
        _p.kind = Kind;                                                                                                \
        _p.flags = Flags;                                                                                              \
        _p.defaultValue = DefaultValue;                                                                                \
        _p.hints = Hints;                                                                                              \
        _p.getter = Getter;                                                                                            \
        _p.setter = Setter;                                                                                            \
        _myengine_type_descriptor.properties.push_back(std::move(_p));                                                 \
    } while (false)
#define MYENGINE_MIGRATION(FromVersion, MigrationFunction)                                                             \
    _myengine_type_descriptor.migrations.emplace(FromVersion, MigrationFunction)
#define MYENGINE_CUSTOM_INSPECTOR(DrawFunction) _myengine_type_descriptor.customInspector = DrawFunction
#define MYENGINE_SCRIPT_BINDER(BindFunction) _myengine_type_descriptor.scriptBinder = BindFunction
#define MYENGINE_END_COMPONENT_TYPE() _myengine_type_descriptor
