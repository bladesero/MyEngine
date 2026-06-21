#include "Scripting/AngelScriptRuntime.h"

#include "Input/Input.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#include <angelscript.h>
#include <scriptstdstring.h>

#include <sstream>
#include <utility>

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

    asITypeInfo* type = module->GetTypeInfoByName(className.c_str());
    if (!type) {
        error = "AngelScript class not found: " + className;
        return false;
    }
    m_Impl->object = static_cast<asIScriptObject*>(
        m_Impl->engine->CreateScriptObject(type));
    if (!m_Impl->object) {
        error = "failed to instantiate AngelScript class: " + className;
        return false;
    }

    m_Impl->awake = type->GetMethodByDecl("void Awake()");
    m_Impl->onEnable = type->GetMethodByDecl("void OnEnable()");
    m_Impl->start = type->GetMethodByDecl("void Start()");
    m_Impl->fixedUpdate = type->GetMethodByDecl("void FixedUpdate(float)");
    m_Impl->update = type->GetMethodByDecl("void Update(float)");
    m_Impl->lateUpdate = type->GetMethodByDecl("void LateUpdate(float)");
    m_Impl->onCollision = type->GetMethodByDecl("void OnCollision(const CollisionEvent &in)");
    if (!m_Impl->onCollision) m_Impl->onCollision = type->GetMethodByDecl("void OnCollision(CollisionEvent)");
    m_Impl->onDisable = type->GetMethodByDecl("void OnDisable()");
    m_Impl->onDestroy = type->GetMethodByDecl("void OnDestroy()");
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
    return m_Impl->properties;
}

const nlohmann::json& AngelScriptRuntime::GetState() const
{
    return m_Impl->state;
}
