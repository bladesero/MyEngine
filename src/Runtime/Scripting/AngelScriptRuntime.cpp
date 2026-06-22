#include "Scripting/AngelScriptRuntime.h"

#include "Input/Input.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#include <angelscript.h>
#include <scriptstdstring.h>

#include <cstdint>
#include <sstream>
#include <utility>
#include <vector>

namespace {
thread_local ScriptComponent* g_ActiveComponent = nullptr;

ScriptComponent* ActiveComponent()
{
    return g_ActiveComponent;
}

Actor* ActiveActor()
{
    ScriptComponent* component = ActiveComponent();
    return component ? component->GetOwner() : nullptr;
}

Vec3 ReadActorPosition()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetTransform().position : Vec3{};
}

std::string ActorGetName()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetName() : std::string{};
}

void ActorSetPosition(Vec3 value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().position = value;
}

void ActorTranslate(Vec3 value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().position += value;
}

void ActorRotate(Vec3 value)
{
    if (Actor* actor = ActiveActor()) actor->GetTransform().rotation += value;
}

RigidBodyComponent* ActiveBody()
{
    Actor* actor = ActiveActor();
    return actor ? actor->GetComponent<RigidBodyComponent>() : nullptr;
}

void BodySetVelocity(Vec3 value) { if (auto* body = ActiveBody()) body->SetVelocity(value); }
void BodyAddForce(Vec3 value) { if (auto* body = ActiveBody()) body->AddForce(value); }
void BodySetAngularVelocity(Vec3 value) { if (auto* body = ActiveBody()) body->SetAngularVelocity(value); }
void BodyAddTorque(Vec3 value) { if (auto* body = ActiveBody()) body->AddTorque(value); }
void BodyAddImpulse(Vec3 value) { if (auto* body = ActiveBody()) body->AddImpulse(value); }
void BodyAddAngularImpulse(Vec3 value) { if (auto* body = ActiveBody()) body->AddAngularImpulse(value); }
void BodyTeleport(Vec3 position, Vec3 rotation) { if (auto* body = ActiveBody()) body->Teleport(position, rotation); }
void BodySetKinematicTarget(Vec3 position, Vec3 rotation) { if (auto* body = ActiveBody()) body->SetKinematicTarget(position, rotation); }

bool InputActionDown(const std::string& action) { return Input::IsActionDown(action); }
bool InputActionPressed(const std::string& action) { return Input::IsActionPressed(action); }
bool InputActionReleased(const std::string& action) { return Input::IsActionReleased(action); }
float InputAxis(const std::string& action) { return Input::GetAxis1D(action); }
Math::Vec2 InputAxis2(const std::string& action) { return Input::GetAxis2D(action); }

struct ScriptRaycastHit {
    uint64_t actorHandle = 0;
    float distance = 0.0f;
    Vec3 point;
    Vec3 normal;
    bool hit = false;
};

ScriptRaycastHit PhysicsRaycast(Vec3 origin, Vec3 direction, float distance, uint32_t mask)
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

struct ScriptCollisionEvent {
    uint64_t otherHandle = 0;
    Vec3 point;
    Vec3 normal;
    float depth = 0.0f;
    bool trigger = false;
    int phase = 0;
};

void ConstructVec2(void* memory) { new (memory) Math::Vec2(); }
void ConstructVec2(float x, float y, void* memory) { new (memory) Math::Vec2{x, y}; }
void ConstructVec3(void* memory) { new (memory) Vec3(); }
void ConstructVec3(float x, float y, float z, void* memory) { new (memory) Vec3{x, y, z}; }
void ConstructRaycastHit(void* memory) { new (memory) ScriptRaycastHit(); }
void ConstructCollisionEvent(void* memory) { new (memory) ScriptCollisionEvent(); }

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
        asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<Math::Vec2>()));
    Check(engine.RegisterObjectBehaviour("Vec2", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructVec2, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("Vec2", asBEHAVE_CONSTRUCT, "void f(float, float)",
        asFUNCTIONPR(ConstructVec2, (float, float, void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("Vec2", "float x", asOFFSET(Math::Vec2, x)));
    Check(engine.RegisterObjectProperty("Vec2", "float y", asOFFSET(Math::Vec2, y)));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opAdd(const Vec2 &in) const",
        asMETHODPR(Math::Vec2, operator+, (const Math::Vec2&) const, Math::Vec2), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opSub(const Vec2 &in) const",
        asMETHODPR(Math::Vec2, operator-, (const Math::Vec2&) const, Math::Vec2), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec2", "Vec2 opMul(float) const",
        asMETHODPR(Math::Vec2, operator*, (float) const, Math::Vec2), asCALL_THISCALL));
}

void RegisterVec3(asIScriptEngine& engine)
{
    Check(engine.RegisterObjectType("Vec3", sizeof(Vec3),
        asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<Vec3>()));
    Check(engine.RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTIONPR(ConstructVec3, (void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectBehaviour("Vec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)",
        asFUNCTIONPR(ConstructVec3, (float, float, float, void*), void), asCALL_CDECL_OBJLAST));
    Check(engine.RegisterObjectProperty("Vec3", "float x", asOFFSET(Vec3, x)));
    Check(engine.RegisterObjectProperty("Vec3", "float y", asOFFSET(Vec3, y)));
    Check(engine.RegisterObjectProperty("Vec3", "float z", asOFFSET(Vec3, z)));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opAdd(const Vec3 &in) const",
        asMETHODPR(Vec3, operator+, (const Vec3&) const, Vec3), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opSub(const Vec3 &in) const",
        asMETHODPR(Vec3, operator-, (const Vec3&) const, Vec3), asCALL_THISCALL));
    Check(engine.RegisterObjectMethod("Vec3", "Vec3 opMul(float) const",
        asMETHODPR(Vec3, operator*, (float) const, Vec3), asCALL_THISCALL));
}

void RegisterScriptTypes(asIScriptEngine& engine)
{
    RegisterVec2(engine);
    RegisterVec3(engine);

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
}

void RegisterScriptBindings(asIScriptEngine& engine)
{
    RegisterStdString(&engine);
    RegisterScriptTypes(engine);

    engine.SetDefaultNamespace("Actor");
    Check(engine.RegisterGlobalFunction("string GetName()",
        asFUNCTION(ActorGetName), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec3 GetPosition()",
        asFUNCTION(ReadActorPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetPosition(Vec3)",
        asFUNCTION(ActorSetPosition), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Translate(Vec3)",
        asFUNCTION(ActorTranslate), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Rotate(Vec3)",
        asFUNCTION(ActorRotate), asCALL_CDECL));

    engine.SetDefaultNamespace("RigidBody");
    Check(engine.RegisterGlobalFunction("void SetVelocity(Vec3)",
        asFUNCTION(BodySetVelocity), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void AddForce(Vec3)",
        asFUNCTION(BodyAddForce), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetAngularVelocity(Vec3)",
        asFUNCTION(BodySetAngularVelocity), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void AddTorque(Vec3)",
        asFUNCTION(BodyAddTorque), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void AddImpulse(Vec3)",
        asFUNCTION(BodyAddImpulse), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void AddAngularImpulse(Vec3)",
        asFUNCTION(BodyAddAngularImpulse), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void Teleport(Vec3, Vec3)",
        asFUNCTION(BodyTeleport), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("void SetKinematicTarget(Vec3, Vec3)",
        asFUNCTION(BodySetKinematicTarget), asCALL_CDECL));

    engine.SetDefaultNamespace("Input");
    Check(engine.RegisterGlobalFunction("bool ActionDown(const string &in)",
        asFUNCTION(InputActionDown), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool ActionPressed(const string &in)",
        asFUNCTION(InputActionPressed), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("bool ActionReleased(const string &in)",
        asFUNCTION(InputActionReleased), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("float Axis(const string &in)",
        asFUNCTION(InputAxis), asCALL_CDECL));
    Check(engine.RegisterGlobalFunction("Vec2 Axis2(const string &in)",
        asFUNCTION(InputAxis2), asCALL_CDECL));

    engine.SetDefaultNamespace("Physics");
    Check(engine.RegisterGlobalFunction("RaycastHit Raycast(Vec3, Vec3, float distance = 1000.0f, uint mask = 0xffffffff)",
        asFUNCTION(PhysicsRaycast), asCALL_CDECL));
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

        g_ActiveComponent = &component;
        const int result = context->Execute();
        g_ActiveComponent = nullptr;

        if (result != asEXECUTION_FINISHED) {
            std::ostringstream stream;
            stream << "AngelScript execution failed";
            if (result == asEXECUTION_EXCEPTION) {
                stream << ": " << context->GetExceptionString();
                if (asIScriptFunction* exceptionFunction = context->GetExceptionFunction()) {
                    stream << " at " << exceptionFunction->GetDeclaration()
                           << ":" << context->GetExceptionLineNumber();
                }
            }
            error = stream.str();
            context->Release();
            return false;
        }
        context->Release();
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
    asIScriptFunction* onDisable = nullptr;
    asIScriptFunction* onDestroy = nullptr;
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json state = nlohmann::json::object();
    std::vector<ScriptFieldInfo> fields;
    std::ostringstream messages;
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
