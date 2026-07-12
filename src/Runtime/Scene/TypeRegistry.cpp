#include "Scene/TypeRegistry.h"
#include "Scene/Component.h"

#include <algorithm>
#include <exception>
#include <unordered_set>

namespace { void SetError(std::string* out, const std::string& value) { if (out) *out = value; } }

TypeRegistry& TypeRegistry::Get() { static TypeRegistry value; return value; }
TypeId TypeRegistry::StableId(const std::string& text) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}
bool TypeRegistry::Register(TypeDescriptor descriptor, std::string* error) {
    if (m_Frozen) { SetError(error, "type registry is frozen"); return false; }
    if (descriptor.stableName.empty() || !descriptor.factory) { SetError(error, "type requires a stable name and factory"); return false; }
    if (!descriptor.id) descriptor.id = StableId(descriptor.stableName);
    if (m_Types.count(descriptor.stableName) || m_TypeIds.count(descriptor.id)) { SetError(error, "duplicate type name or id: " + descriptor.stableName); return false; }
    std::unordered_set<std::string> names;
    std::unordered_set<PropertyId> ids;
    for (auto& property : descriptor.properties) {
        if (property.stableName.empty() || !property.getter) { SetError(error, "property requires a name and getter"); return false; }
        if (!property.id) property.id = StableId(descriptor.stableName + "." + property.stableName);
        if (!names.insert(property.stableName).second || !ids.insert(property.id).second) { SetError(error, "duplicate property in " + descriptor.stableName); return false; }
        if (HasPropertyFlag(property.flags, PropertyFlags::Serialize) && !property.setter && !HasPropertyFlag(property.flags, PropertyFlags::ReadOnly)) {
            SetError(error, "serializable property requires a setter: " + property.stableName); return false;
        }
    }
    m_TypeIds.emplace(descriptor.id, descriptor.stableName);
    m_Types.emplace(descriptor.stableName, std::move(descriptor));
    return true;
}
bool TypeRegistry::Freeze(std::string* error) {
    for (const auto& [name, type] : m_Types) {
        for (uint32_t version = 0; version < type.schemaVersion; ++version) {
            if (!type.migrations.empty() && !type.migrations.count(version)) { SetError(error, "missing migration " + name + " v" + std::to_string(version)); return false; }
        }
    }
    m_Frozen = true; return true;
}
const TypeDescriptor* TypeRegistry::Find(const std::string& name) const { auto it=m_Types.find(name); return it==m_Types.end()?nullptr:&it->second; }
const TypeDescriptor* TypeRegistry::Find(TypeId id) const { auto it=m_TypeIds.find(id); return it==m_TypeIds.end()?nullptr:Find(it->second); }
const PropertyDescriptor* TypeRegistry::FindProperty(const TypeDescriptor& type, const std::string& name) const {
    for (const auto& p : type.properties) { if (p.stableName == name || std::find(p.aliases.begin(),p.aliases.end(),name)!=p.aliases.end()) return &p; }
    return nullptr;
}
std::unique_ptr<Component> TypeRegistry::Create(const std::string& name) const { const auto* type=Find(name); return type?type->factory():nullptr; }
std::vector<std::string> TypeRegistry::GetRegisteredTypes() const { std::vector<std::string> v; for(const auto& e:m_Types)v.push_back(e.first); std::sort(v.begin(),v.end()); return v; }
std::vector<std::string> TypeRegistry::GetIncompleteMetadataTypes() const { std::vector<std::string> v; for(const auto& [name,type]:m_Types)if(!type.metadataComplete)v.push_back(name);std::sort(v.begin(),v.end());return v; }
bool TypeRegistry::Serialize(const Component& component, nlohmann::json& data, uint32_t& version, std::string* error) const {
    const auto* type=Find(component.GetTypeName()); if(!type){SetError(error,"unregistered component");return false;}
    version=type->schemaVersion; data=component.GetExtensionData();
    if (type->properties.empty()) { component.Serialize(data); return true; }
    nlohmann::json properties=nlohmann::json::object();
    try { for(const auto& p:type->properties) if(HasPropertyFlag(p.flags,PropertyFlags::Serialize)&&!HasPropertyFlag(p.flags,PropertyFlags::Transient)){nlohmann::json value=p.getter(component);if(!value.is_null())properties[p.stableName]=std::move(value);} }
    catch(const std::exception& e){SetError(error,e.what());return false;}
    data["properties"]=std::move(properties); return true;
}
bool TypeRegistry::Deserialize(Component& component,const std::string& name,uint32_t version,const nlohmann::json& input,std::string* error) const {
    const auto* type=Find(name); if(!type){SetError(error,"unregistered component: "+name);return false;}
    nlohmann::json data=input;
    if(version>type->schemaVersion){component.SetExtensionData(data);SetError(error,"component schema is newer than runtime");return false;}
    const uint32_t sourceVersion=version;
    for(uint32_t v=version;v<type->schemaVersion;++v){auto it=type->migrations.find(v);if(it!=type->migrations.end()&&!it->second(data,error))return false;}
    if(type->properties.empty()){try{component.Deserialize(data);component.SetExtensionData(nlohmann::json::object());return true;}catch(const std::exception& e){SetError(error,e.what());return false;}}
    if(sourceVersion==0){try{component.Deserialize(data);component.SetExtensionData(nlohmann::json::object());return true;}catch(const std::exception& e){SetError(error,e.what());return false;}}
    const nlohmann::json& values=data.contains("properties")?data["properties"]:data;
    if(type->legacyDeserialize){try{nlohmann::json materialized=nlohmann::json::object();for(const auto& p:type->properties){if(!HasPropertyFlag(p.flags,PropertyFlags::Serialize))continue;const nlohmann::json* value=nullptr;if(values.contains(p.stableName))value=&values[p.stableName];else for(const auto& alias:p.aliases)if(values.contains(alias)){value=&values[alias];break;}if(!value&&!p.defaultValue.is_discarded())value=&p.defaultValue;if(value)materialized[p.stableName]=*value;}type->legacyDeserialize(component,materialized);nlohmann::json extension=data;extension.erase("properties");component.SetExtensionData(std::move(extension));return true;}catch(const std::exception& e){SetError(error,e.what());return false;}}
    try { for(const auto& p:type->properties){if(!HasPropertyFlag(p.flags,PropertyFlags::Serialize)||!p.setter)continue;const nlohmann::json* value=nullptr;if(values.contains(p.stableName))value=&values[p.stableName];else for(const auto& alias:p.aliases)if(values.contains(alias)){value=&values[alias];break;}if(!value&&!p.defaultValue.is_discarded())value=&p.defaultValue;if(value&&!p.setter(component,*value,error))return false;} }
    catch(const std::exception& e){SetError(error,e.what());return false;}
    nlohmann::json extension=data; extension.erase("properties"); component.SetExtensionData(std::move(extension)); return true;
}
bool TypeRegistry::GetProperty(const Component& c,const std::string& name,nlohmann::json& value) const {const auto* t=Find(c.GetTypeName());const auto* p=t?FindProperty(*t,name):nullptr;if(!p)return false;value=p->getter(c);return true;}
bool TypeRegistry::SetProperty(Component& c,const std::string& name,const nlohmann::json& value,std::string* error) const {const auto* t=Find(c.GetTypeName());const auto* p=t?FindProperty(*t,name):nullptr;if(!p||!p->setter||HasPropertyFlag(p->flags,PropertyFlags::ReadOnly)){SetError(error,"property is not writable");return false;}return p->setter(c,value,error);}
