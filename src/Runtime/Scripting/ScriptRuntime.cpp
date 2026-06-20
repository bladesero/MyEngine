#include "Scripting/ScriptRuntime.h"

#include "Input/Input.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#include <lua.hpp>

#include <algorithm>
#include <cstring>

namespace {

ScriptComponent* GetComponent(lua_State* state)
{
    return static_cast<ScriptComponent*>(lua_touserdata(state, lua_upvalueindex(1)));
}

Actor* GetActor(lua_State* state)
{
    ScriptComponent* component = GetComponent(state);
    return component ? component->GetOwner() : nullptr;
}

int Traceback(lua_State* state)
{
    const char* message = lua_tostring(state, 1);
    if (message) luaL_traceback(state, state, message, 1);
    else lua_pushliteral(state, "Lua error without a message");
    return 1;
}

void PushVec3(lua_State* state, const Vec3& value)
{
    lua_createtable(state, 0, 3);
    lua_pushnumber(state, value.x); lua_setfield(state, -2, "x");
    lua_pushnumber(state, value.y); lua_setfield(state, -2, "y");
    lua_pushnumber(state, value.z); lua_setfield(state, -2, "z");
}

void PushJson(lua_State* state, const nlohmann::json& value)
{
    if (value.is_null()) {
        lua_pushnil(state);
    } else if (value.is_boolean()) {
        lua_pushboolean(state, value.get<bool>());
    } else if (value.is_number()) {
        lua_pushnumber(state, value.get<double>());
    } else if (value.is_string()) {
        const std::string text = value.get<std::string>();
        lua_pushlstring(state, text.data(), text.size());
    } else if (value.is_array()) {
        lua_createtable(state, static_cast<int>(value.size()), 0);
        int index = 1;
        for (const auto& item : value) {
            PushJson(state, item);
            lua_rawseti(state, -2, index++);
        }
    } else if (value.is_object()) {
        lua_createtable(state, 0, static_cast<int>(value.size()));
        for (auto it = value.begin(); it != value.end(); ++it) {
            PushJson(state, it.value());
            lua_setfield(state, -2, it.key().c_str());
        }
    } else {
        lua_pushnil(state);
    }
}

nlohmann::json ReadJson(lua_State* state, int index, int depth = 0)
{
    if (depth > 16) return nullptr;
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL: return nullptr;
    case LUA_TBOOLEAN: return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER: return lua_tonumber(state, index);
    case LUA_TSTRING:
        return std::string(lua_tostring(state, index));
    case LUA_TTABLE: {
        bool array = true;
        size_t maxIndex = 0;
        size_t count = 0;
        lua_pushnil(state);
        while (lua_next(state, index) != 0) {
            ++count;
            if (!lua_isinteger(state, -2) || lua_tointeger(state, -2) <= 0) {
                array = false;
            } else {
                maxIndex = (std::max)(maxIndex, static_cast<size_t>(lua_tointeger(state, -2)));
            }
            lua_pop(state, 1);
        }
        if (array && maxIndex == count) {
            nlohmann::json result = nlohmann::json::array();
            for (size_t i = 1; i <= maxIndex; ++i) {
                lua_rawgeti(state, index, static_cast<lua_Integer>(i));
                result.push_back(ReadJson(state, -1, depth + 1));
                lua_pop(state, 1);
            }
            return result;
        }
        nlohmann::json result = nlohmann::json::object();
        lua_pushnil(state);
        while (lua_next(state, index) != 0) {
            if (lua_type(state, -2) == LUA_TSTRING) {
                result[lua_tostring(state, -2)] = ReadJson(state, -1, depth + 1);
            }
            lua_pop(state, 1);
        }
        return result;
    }
    default:
        return nullptr;
    }
}

void SetBoundFunction(lua_State* state, ScriptComponent& component,
                      const char* name, lua_CFunction function)
{
    lua_pushlightuserdata(state, &component);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

int ActorGetPosition(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (!actor) return 0;
    PushVec3(state, actor->GetTransform().position);
    return 1;
}

int ActorSetPosition(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (actor) actor->GetTransform().position = {
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3))
    };
    return 0;
}

int ActorTranslate(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (actor) actor->GetTransform().position += {
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3))
    };
    return 0;
}

int ActorRotate(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (actor) actor->GetTransform().rotation += {
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3))
    };
    return 0;
}

int ActorGetName(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (!actor) return 0;
    lua_pushlstring(state, actor->GetName().data(), actor->GetName().size());
    return 1;
}

int BodySetVelocity(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (actor) {
        if (auto* body = actor->GetComponent<RigidBodyComponent>()) {
            body->SetVelocity({
                static_cast<float>(luaL_checknumber(state, 1)),
                static_cast<float>(luaL_checknumber(state, 2)),
                static_cast<float>(luaL_checknumber(state, 3))
            });
        }
    }
    return 0;
}

int BodyAddForce(lua_State* state)
{
    Actor* actor = GetActor(state);
    if (actor) {
        if (auto* body = actor->GetComponent<RigidBodyComponent>()) {
            body->AddForce({
                static_cast<float>(luaL_checknumber(state, 1)),
                static_cast<float>(luaL_checknumber(state, 2)),
                static_cast<float>(luaL_checknumber(state, 3))
            });
        }
    }
    return 0;
}

RigidBodyComponent* GetRigidBody(lua_State* state)
{
    Actor* actor = GetActor(state);
    return actor ? actor->GetComponent<RigidBodyComponent>() : nullptr;
}

Vec3 ReadVec3(lua_State* state, int first)
{
    return {static_cast<float>(luaL_checknumber(state, first)),
            static_cast<float>(luaL_checknumber(state, first + 1)),
            static_cast<float>(luaL_checknumber(state, first + 2))};
}

int BodySetAngularVelocity(lua_State* state) { if (auto* body = GetRigidBody(state)) body->SetAngularVelocity(ReadVec3(state, 1)); return 0; }
int BodyAddTorque(lua_State* state) { if (auto* body = GetRigidBody(state)) body->AddTorque(ReadVec3(state, 1)); return 0; }
int BodyAddImpulse(lua_State* state) { if (auto* body = GetRigidBody(state)) body->AddImpulse(ReadVec3(state, 1)); return 0; }
int BodyAddAngularImpulse(lua_State* state) { if (auto* body = GetRigidBody(state)) body->AddAngularImpulse(ReadVec3(state, 1)); return 0; }
int BodyTeleport(lua_State* state) {
    if (auto* body = GetRigidBody(state)) body->Teleport(ReadVec3(state, 1), ReadVec3(state, 4));
    return 0;
}
int BodySetKinematicTarget(lua_State* state) {
    if (auto* body = GetRigidBody(state)) body->SetKinematicTarget(ReadVec3(state, 1), ReadVec3(state, 4));
    return 0;
}

int InputKeyDown(lua_State* state)
{
    lua_pushboolean(state, Input::IsKeyDown(static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int InputKeyPressed(lua_State* state)
{
    lua_pushboolean(state, Input::IsKeyPressed(static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int InputMouseDown(lua_State* state)
{
    lua_pushboolean(state, Input::IsMouseDown(static_cast<int>(luaL_checkinteger(state, 1))));
    return 1;
}

int PhysicsRaycast(lua_State* state)
{
    Actor* actor = GetActor(state);
    Scene* scene = actor ? actor->GetScene() : nullptr;
    if (!scene) {
        lua_pushnil(state);
        return 1;
    }
    Ray ray;
    ray.origin = {
        static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)),
        static_cast<float>(luaL_checknumber(state, 3))
    };
    ray.direction = {
        static_cast<float>(luaL_checknumber(state, 4)),
        static_cast<float>(luaL_checknumber(state, 5)),
        static_cast<float>(luaL_checknumber(state, 6))
    };
    const float distance = static_cast<float>(luaL_optnumber(state, 7, 1000.0));
    const uint32_t mask = static_cast<uint32_t>(luaL_optinteger(state, 8, 0xffffffff));
    RaycastHit hit;
    if (!scene->GetPhysicsWorld().Raycast(*scene, ray, distance, mask, hit)) {
        lua_pushnil(state);
        return 1;
    }
    lua_createtable(state, 0, 5);
    lua_pushinteger(state, static_cast<lua_Integer>(hit.actor->GetHandle().ToUInt64()));
    lua_setfield(state, -2, "actorHandle");
    lua_pushlstring(state, hit.actor->GetName().data(), hit.actor->GetName().size());
    lua_setfield(state, -2, "actorName");
    lua_pushnumber(state, hit.distance); lua_setfield(state, -2, "distance");
    PushVec3(state, hit.point); lua_setfield(state, -2, "point");
    PushVec3(state, hit.normal); lua_setfield(state, -2, "normal");
    return 1;
}

void CreateBindings(lua_State* state, ScriptComponent& component)
{
    lua_createtable(state, 0, 5);
    SetBoundFunction(state, component, "get_position", ActorGetPosition);
    SetBoundFunction(state, component, "set_position", ActorSetPosition);
    SetBoundFunction(state, component, "translate", ActorTranslate);
    SetBoundFunction(state, component, "rotate", ActorRotate);
    SetBoundFunction(state, component, "get_name", ActorGetName);
    lua_setglobal(state, "Actor");

    lua_createtable(state, 0, 8);
    SetBoundFunction(state, component, "set_velocity", BodySetVelocity);
    SetBoundFunction(state, component, "add_force", BodyAddForce);
    SetBoundFunction(state, component, "set_angular_velocity", BodySetAngularVelocity);
    SetBoundFunction(state, component, "add_torque", BodyAddTorque);
    SetBoundFunction(state, component, "add_impulse", BodyAddImpulse);
    SetBoundFunction(state, component, "add_angular_impulse", BodyAddAngularImpulse);
    SetBoundFunction(state, component, "teleport", BodyTeleport);
    SetBoundFunction(state, component, "set_kinematic_target", BodySetKinematicTarget);
    lua_setglobal(state, "RigidBody");

    lua_createtable(state, 0, 3);
    SetBoundFunction(state, component, "is_key_down", InputKeyDown);
    SetBoundFunction(state, component, "is_key_pressed", InputKeyPressed);
    SetBoundFunction(state, component, "is_mouse_down", InputMouseDown);
    lua_setglobal(state, "Input");

    lua_createtable(state, 0, 1);
    SetBoundFunction(state, component, "raycast", PhysicsRaycast);
    lua_setglobal(state, "Physics");
}

bool ProtectedCall(lua_State* state, int argumentCount, std::string& error)
{
    const int functionIndex = lua_gettop(state) - argumentCount;
    lua_pushcfunction(state, Traceback);
    lua_insert(state, functionIndex);
    if (lua_pcall(state, argumentCount, 0, functionIndex) != LUA_OK) {
        error = lua_tostring(state, -1);
        lua_pop(state, 2);
        return false;
    }
    lua_remove(state, functionIndex);
    return true;
}

} // namespace

struct ScriptRuntime::Impl {
    explicit Impl(ScriptComponent& owner) : component(owner) {}
    ~Impl() { if (state) lua_close(state); }

    ScriptComponent& component;
    lua_State* state = nullptr;
};

ScriptRuntime::ScriptRuntime(ScriptComponent& component)
    : m_Impl(std::make_unique<Impl>(component))
{
}

ScriptRuntime::~ScriptRuntime() = default;

bool ScriptRuntime::Load(const std::string& source, const std::string& chunkName,
                         const nlohmann::json& inspector, const nlohmann::json& stateData,
                         std::string& error)
{
    if (m_Impl->state) lua_close(m_Impl->state);
    m_Impl->state = luaL_newstate();
    if (!m_Impl->state) {
        error = "failed to create Lua state";
        return false;
    }
    luaL_openlibs(m_Impl->state);
    CreateBindings(m_Impl->state, m_Impl->component);

    if (luaL_loadbuffer(m_Impl->state, source.data(), source.size(), chunkName.c_str()) != LUA_OK) {
        error = lua_tostring(m_Impl->state, -1);
        lua_pop(m_Impl->state, 1);
        return false;
    }
    if (!ProtectedCall(m_Impl->state, 0, error)) return false;

    if (inspector.is_object() && !inspector.empty()) {
        PushJson(m_Impl->state, inspector);
        lua_setglobal(m_Impl->state, "Inspector");
    } else {
        lua_getglobal(m_Impl->state, "Inspector");
        if (!lua_istable(m_Impl->state, -1)) {
            lua_pop(m_Impl->state, 1);
            lua_newtable(m_Impl->state);
            lua_setglobal(m_Impl->state, "Inspector");
        } else lua_pop(m_Impl->state, 1);
    }
    if (stateData.is_object() && !stateData.empty()) {
        PushJson(m_Impl->state, stateData);
        lua_setglobal(m_Impl->state, "State");
    } else {
        lua_getglobal(m_Impl->state, "State");
        if (!lua_istable(m_Impl->state, -1)) {
            lua_pop(m_Impl->state, 1);
            lua_newtable(m_Impl->state);
            lua_setglobal(m_Impl->state, "State");
        } else lua_pop(m_Impl->state, 1);
    }
    return true;
}

bool ScriptRuntime::Call(const char* functionName, float deltaSeconds, std::string& error)
{
    lua_getglobal(m_Impl->state, functionName);
    if (!lua_isfunction(m_Impl->state, -1)) {
        lua_pop(m_Impl->state, 1);
        return true;
    }
    lua_pushnumber(m_Impl->state, deltaSeconds);
    return ProtectedCall(m_Impl->state, 1, error);
}

bool ScriptRuntime::CallCollision(const CollisionEvent& event, std::string& error)
{
    lua_getglobal(m_Impl->state, "OnCollision");
    if (!lua_isfunction(m_Impl->state, -1)) {
        lua_pop(m_Impl->state, 1);
        return true;
    }
    lua_createtable(m_Impl->state, 0, 7);
    if (event.other) {
        lua_pushinteger(m_Impl->state, static_cast<lua_Integer>(event.otherHandle.ToUInt64()));
        lua_setfield(m_Impl->state, -2, "otherHandle");
        lua_pushlstring(m_Impl->state, event.other->GetName().data(), event.other->GetName().size());
        lua_setfield(m_Impl->state, -2, "otherName");
    }
    PushVec3(m_Impl->state, event.point); lua_setfield(m_Impl->state, -2, "point");
    PushVec3(m_Impl->state, event.normal); lua_setfield(m_Impl->state, -2, "normal");
    lua_pushnumber(m_Impl->state, event.depth); lua_setfield(m_Impl->state, -2, "depth");
    lua_pushboolean(m_Impl->state, event.trigger); lua_setfield(m_Impl->state, -2, "trigger");
    const char* phase = event.phase == CollisionEventPhase::Enter ? "Enter"
        : event.phase == CollisionEventPhase::Stay ? "Stay" : "Exit";
    lua_pushstring(m_Impl->state, phase); lua_setfield(m_Impl->state, -2, "phase");
    return ProtectedCall(m_Impl->state, 1, error);
}

nlohmann::json ScriptRuntime::CaptureTable(const char* tableName) const
{
    lua_getglobal(m_Impl->state, tableName);
    nlohmann::json result = lua_istable(m_Impl->state, -1)
        ? ReadJson(m_Impl->state, -1) : nlohmann::json::object();
    lua_pop(m_Impl->state, 1);
    return result;
}
