#include "Editor/EditorLuaScriptService.h"

#include "Core/Logger.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <fstream>
#include <lua.hpp>
#include <sstream>

namespace {
EditorContext* GetContext(lua_State* state) {
    return static_cast<EditorContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

void SetError(std::string* error, std::string message) {
    if (error)
        *error = std::move(message);
}

int Traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message)
        luaL_traceback(state, state, message, 1);
    else
        lua_pushliteral(state, "Lua editor script failed");
    return 1;
}

bool ExecuteSnapshotCommand(EditorContext& context, const char* name, const std::string& beforeJson,
                            const std::string& afterJson, uint64_t beforeSelection, uint64_t afterSelection) {
    auto command = std::make_unique<SceneSnapshotCommand>(name, beforeJson, afterJson, beforeSelection, afterSelection);
    if (EditorCommandStack* stack = context.GetCommandStack()) {
        return stack->ExecuteCommand(std::move(command), context);
    }
    return command->Execute(context);
}

int EditorLog(lua_State* state) {
    const char* message = luaL_checkstring(state, 1);
    Logger::Info("[EditorLua] ", message ? message : "");
    return 0;
}

int SelectionGetActorID(lua_State* state) {
    EditorContext* context = GetContext(state);
    lua_pushinteger(state, context ? static_cast<lua_Integer>(context->GetSelection().GetActorID()) : 0);
    return 1;
}

int SelectionSelectActor(lua_State* state) {
    EditorContext* context = GetContext(state);
    if (context)
        context->GetSelection().SelectActorID(static_cast<uint64_t>(luaL_checkinteger(state, 1)));
    return 0;
}

int SelectionGetAssetPath(lua_State* state) {
    EditorContext* context = GetContext(state);
    const std::string& path = context ? context->GetSelection().GetAssetPath() : std::string{};
    lua_pushlstring(state, path.data(), path.size());
    return 1;
}

int SceneCreateActor(lua_State* state) {
    EditorContext* context = GetContext(state);
    Scene* scene = context ? context->GetScene() : nullptr;
    if (!context || !scene) {
        lua_pushinteger(state, 0);
        return 1;
    }
    const char* name = luaL_optstring(state, 1, "Actor");
    const uint64_t beforeSelection = context->GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    Actor* actor = scene->CreateActor(name ? name : "Actor");
    const uint64_t createdID = actor ? actor->GetID() : 0;
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    ExecuteSnapshotCommand(*context, "Lua Create Actor", before, after, beforeSelection, createdID);
    lua_pushinteger(state, static_cast<lua_Integer>(createdID));
    return 1;
}

int SceneDestroySelected(lua_State* state) {
    EditorContext* context = GetContext(state);
    Scene* scene = context ? context->GetScene() : nullptr;
    Actor* actor = scene ? context->GetSelection().ResolveActor(*scene) : nullptr;
    if (!context || !scene || !actor) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const uint64_t beforeSelection = context->GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    scene->DestroyActor(actor);
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    const bool ok = ExecuteSnapshotCommand(*context, "Lua Destroy Actor", before, after, beforeSelection, 0);
    lua_pushboolean(state, ok ? 1 : 0);
    return 1;
}

int SceneSetSelectedPosition(lua_State* state) {
    EditorContext* context = GetContext(state);
    Scene* scene = context ? context->GetScene() : nullptr;
    Actor* actor = scene ? context->GetSelection().ResolveActor(*scene) : nullptr;
    if (!context || !scene || !actor)
        return 0;
    Transform before = actor->GetTransform();
    Transform after = before;
    after.position = {static_cast<float>(luaL_checknumber(state, 1)), static_cast<float>(luaL_checknumber(state, 2)),
                      static_cast<float>(luaL_checknumber(state, 3))};
    auto command = std::make_unique<SetActorTransformCommand>(actor->GetID(), before, after);
    if (EditorCommandStack* stack = context->GetCommandStack())
        stack->ExecuteCommand(std::move(command), *context);
    else
        command->Execute(*context);
    return 0;
}

int ActionsExecute(lua_State* state) {
    EditorContext* context = GetContext(state);
    const char* id = luaL_checkstring(state, 1);
    EditorActionRegistry* actions = context ? context->GetActionRegistry() : nullptr;
    lua_pushboolean(state, actions && actions->Execute(id ? id : "", *context));
    return 1;
}

void SetBoundFunction(lua_State* state, EditorContext& context, const char* name, lua_CFunction function) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

void CreateBindings(lua_State* state, EditorContext& context) {
    lua_createtable(state, 0, 1);
    SetBoundFunction(state, context, "log", EditorLog);
    lua_setglobal(state, "Editor");

    lua_createtable(state, 0, 3);
    SetBoundFunction(state, context, "get_actor_id", SelectionGetActorID);
    SetBoundFunction(state, context, "select_actor", SelectionSelectActor);
    SetBoundFunction(state, context, "get_asset_path", SelectionGetAssetPath);
    lua_setglobal(state, "Selection");

    lua_createtable(state, 0, 3);
    SetBoundFunction(state, context, "create_actor", SceneCreateActor);
    SetBoundFunction(state, context, "destroy_selected", SceneDestroySelected);
    SetBoundFunction(state, context, "set_selected_position", SceneSetSelectedPosition);
    lua_setglobal(state, "Scene");

    lua_createtable(state, 0, 1);
    SetBoundFunction(state, context, "execute", ActionsExecute);
    lua_setglobal(state, "Actions");
}
} // namespace

EditorLuaScriptService::~EditorLuaScriptService() = default;

bool EditorLuaScriptService::RunSource(const std::string& source, const std::string& chunkName, std::string* error) {
    EditorContext* context = GetContext();
    if (!context) {
        SetError(error, "editor Lua service is not attached");
        return false;
    }
    lua_State* state = luaL_newstate();
    if (!state) {
        SetError(error, "failed to create Lua editor state");
        return false;
    }
    luaL_openlibs(state);
    CreateBindings(state, *context);

    bool ok = true;
    if (luaL_loadbuffer(state, source.data(), source.size(), chunkName.c_str()) != LUA_OK) {
        SetError(error, lua_tostring(state, -1));
        ok = false;
    } else {
        const int functionIndex = lua_gettop(state);
        lua_pushcfunction(state, Traceback);
        lua_insert(state, functionIndex);
        if (lua_pcall(state, 0, 0, functionIndex) != LUA_OK) {
            SetError(error, lua_tostring(state, -1));
            ok = false;
        }
    }
    lua_close(state);
    if (!ok && error && !error->empty())
        Logger::Error("[EditorLua] ", *error);
    return ok;
}

bool EditorLuaScriptService::RunFile(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "failed to open editor Lua script: " + path.string());
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return RunSource(stream.str(), path.string(), error);
}
