#include "Assets/AssetManager.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/Memory/MemoryService.h"
#include "Core/Memory/PoolAllocator.h"
#include "Input/Input.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool NearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool Check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << '\n';
        return false;
    }
    return true;
}

bool TestSceneSerializationRegression() {
    AssetManager::Get().Clear();

    Scene scene("SerializeCase");
    Actor* parent = scene.CreateActor("Parent");
    parent->GetTransform().position = Vec3 { 2.0f, 3.0f, 4.0f };

    Actor* child = scene.CreateActor("Child", parent);
    child->GetTransform().position = Vec3 { 1.0f, 0.0f, 0.0f };

    auto* mr = parent->AddComponent<MeshRendererComponent>();
    mr->SetMesh(AssetManager::Get().GetCubeMesh());
    mr->SetMaterial(AssetManager::Get().GetDefaultMaterial());

    const std::string json = SceneSerializer::SaveToString(scene);

    Scene loaded("Loaded");
    if (!Check(SceneSerializer::LoadFromString(loaded, json), "LoadFromString failed")) return false;

    if (!Check(loaded.GetName() == "SerializeCase", "scene name mismatch")) return false;
    if (!Check(loaded.ActorCount() == 2, "actor count mismatch")) return false;

    Actor* loadedParent = loaded.FindByName("Parent");
    Actor* loadedChild = loaded.FindByName("Child");
    if (!Check(loadedParent != nullptr, "parent not found")) return false;
    if (!Check(loadedChild != nullptr, "child not found")) return false;
    if (!Check(loadedChild->GetParent() == loadedParent, "child-parent relation mismatch")) return false;

    const Vec3 p = loadedParent->GetTransform().position;
    if (!Check(NearlyEqual(p.x, 2.0f) && NearlyEqual(p.y, 3.0f) && NearlyEqual(p.z, 4.0f),
               "parent transform mismatch")) return false;

    auto* loadedMr = loadedParent->GetComponent<MeshRendererComponent>();
    if (!Check(loadedMr != nullptr, "MeshRenderer missing after deserialize")) return false;
    if (!Check(loadedMr->IsValid(), "MeshRenderer handles are invalid after deserialize")) return false;

    return true;
}

bool TestTransformHierarchyWorldPosition() {
    Scene scene("TransformCase");
    Actor* root = scene.CreateActor("Root");
    root->GetTransform().position = Vec3 { 10.0f, 0.0f, 0.0f };

    Actor* child = scene.CreateActor("Child", root);
    child->GetTransform().position = Vec3 { 1.0f, 2.0f, 3.0f };

    const Vec3 world = child->GetWorldPosition();
    return Check(
        NearlyEqual(world.x, 11.0f) &&
        NearlyEqual(world.y, 2.0f) &&
        NearlyEqual(world.z, 3.0f),
        "child world position mismatch");
}

bool TestInputBoundaries() {
    Input::Flush();

    if (!Check(!Input::IsKeyDown(-1), "negative key index should be false")) return false;
    if (!Check(!Input::IsKeyDown(Input::k_MaxKeys), "overflow key index should be false")) return false;
    if (!Check(!Input::IsMouseDown(0), "mouse button 0 should be invalid")) return false;
    if (!Check(!Input::IsMouseDown(Input::k_MaxButtons), "overflow mouse index should be false")) return false;

    const int key = Input::k_MaxKeys - 1;
    Input::OnKeyUp(key);
    Input::Flush();
    Input::OnKeyDown(key);
    if (!Check(Input::IsKeyDown(key), "key should be down")) return false;
    if (!Check(Input::IsKeyPressed(key), "key should be pressed on transition")) return false;
    Input::Flush();
    if (!Check(!Input::IsKeyPressed(key), "key pressed should clear next frame")) return false;
    Input::OnKeyUp(key);
    if (!Check(Input::IsKeyReleased(key), "key release transition failed")) return false;

    const int btn = Input::k_MaxButtons - 1;
    Input::OnMouseButton(btn, false);
    Input::Flush();
    Input::OnMouseButton(btn, true);
    if (!Check(Input::IsMouseDown(btn), "mouse button should be down")) return false;
    if (!Check(Input::IsMousePressed(btn), "mouse button pressed transition failed")) return false;
    Input::Flush();
    Input::OnMouseButton(btn, false);
    if (!Check(Input::IsMouseReleased(btn), "mouse button release transition failed")) return false;

    return true;
}

bool TestGamepadStateTransitions() {
    const SDL_JoystickID pad = 42;

    Input::OnGamepadAdded(pad);
    if (!Check(Input::IsGamepadConnected(pad), "gamepad should be connected after add")) return false;
    if (!Check(Input::GetGamepadCount() == 1, "gamepad count should be 1")) return false;

    Input::Flush();
    Input::OnGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, true);
    if (!Check(Input::IsGamepadButtonDown(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button should be down")) return false;
    if (!Check(Input::IsGamepadButtonPressed(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button press transition failed")) return false;

    Input::Flush();
    if (!Check(!Input::IsGamepadButtonPressed(pad, SDL_GAMEPAD_BUTTON_SOUTH), "pressed should clear next frame")) return false;

    Input::OnGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
    if (!Check(Input::IsGamepadButtonReleased(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button release transition failed")) return false;

    Input::OnGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 16384);
    if (!Check(NearlyEqual(Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), 0.5f, 0.02f),
               "gamepad axis normalization failed")) return false;

    Input::OnGamepadRemoved(pad);
    if (!Check(!Input::IsGamepadConnected(pad), "gamepad should be disconnected after remove")) return false;

    return true;
}

bool TestAssetFileImporters() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_import_test";
    fs::create_directories(root);

    const fs::path texPath = root / "albedo.ppm";
    const fs::path mtlPath = root / "tri.mtl";
    const fs::path objPath = root / "tri.obj";

    {
        std::ofstream tex(texPath, std::ios::binary);
        tex << "P6\n2 2\n255\n";
        const unsigned char pixels[] = {
            255, 0,   0,
            0,   255, 0,
            0,   0,   255,
            255, 255, 255,
        };
        tex.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    {
        std::ofstream mtl(mtlPath, std::ios::binary);
        mtl << "newmtl Material0\n";
        mtl << "Kd 1 1 1\n";
        mtl << "map_Kd albedo.ppm\n";
    }

    {
        std::ofstream obj(objPath, std::ios::binary);
        obj << "mtllib tri.mtl\n";
        obj << "o Tri\n";
        obj << "usemtl Material0\n";
        obj << "v 0 0 0\n";
        obj << "v 1 0 0\n";
        obj << "v 0 1 0\n";
        obj << "vt 0 0\n";
        obj << "vt 1 0\n";
        obj << "vt 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "f 1/1/1 2/2/2 3/3/3\n";
    }

    AssetManager& am = AssetManager::Get();
    auto tex = am.Load<TextureAsset>(texPath.string());
    if (!Check(tex.IsValid(), "texture import should succeed")) return false;
    if (!Check(tex->GetWidth() == 2 && tex->GetHeight() == 2, "texture dimensions mismatch")) return false;
    if (!Check(tex->GetPixelData().size() == 16, "texture pixel data size mismatch")) return false;

    auto model = am.Load<ModelAsset>(objPath.string());
    if (!Check(model.IsValid(), "model import should succeed")) return false;
    if (!Check(model->GetMesh() && model->GetMesh()->VertexCount() == 3, "model vertex count mismatch")) return false;
    if (!Check(model->MaterialCount() == 1, "model material count mismatch")) return false;
    if (!Check(model->GetMaterial(0).IsValid(), "model material should be valid")) return false;
    if (!Check(model->GetMaterial(0)->HasTexture("BaseColorMap"), "material should keep imported texture")) return false;

    am.Clear();
    fs::remove_all(root);
    return true;
}

bool TestMemoryLinearAllocator() {
    LinearAllocator arena;
    if (!Check(arena.Init(4096), "LinearAllocator::Init failed")) return false;
    void* p1 = arena.Allocate(64, 8);
    void* p2 = arena.Allocate(128, 16);
    if (!Check(p1 != nullptr && p2 != nullptr, "LinearAllocator bump failed")) return false;
    if (!Check(reinterpret_cast<std::uintptr_t>(p2) > reinterpret_cast<std::uintptr_t>(p1),
               "LinearAllocator ordering unexpected")) return false;
    arena.Reset();
    void* p3 = arena.Allocate(4000, 8);
    if (!Check(p3 != nullptr, "LinearAllocator reuse after Reset failed")) return false;
    arena.Shutdown();
    return true;
}

bool TestMemoryPoolAllocator() {
    PoolAllocator<int> pool(4);
    int* a = pool.Allocate(1);
    int* b = pool.Allocate(2);
    if (!Check(a && b, "PoolAllocator Allocate failed")) return false;
    if (!Check(pool.LiveCount() == 2, "PoolAllocator live count")) return false;
    pool.Free(a);
    if (!Check(pool.LiveCount() == 1, "PoolAllocator after one Free")) return false;
    int* c = pool.Allocate(3);
    if (!Check(c && *c == 3, "PoolAllocator recycle slot")) return false;
    pool.Free(b);
    pool.Free(c);
    if (!Check(pool.LiveCount() == 0, "PoolAllocator empty")) return false;
    if (!Check(pool.Allocate(42) != nullptr, "PoolAllocator fill after empty")) return false;
    return true;
}

bool TestMemoryServiceHeapRoundTrip() {
    void* p = MemoryService::Get().Allocate(AllocTag::Test, 32, 8, __FILE__, __LINE__);
    if (!Check(p != nullptr, "MemoryService::Allocate failed")) {
        return false;
    }
    std::memset(p, 0xAB, 32);
    MemoryService::Get().Free(p, __FILE__, __LINE__);
    return true;
}

bool TestSceneAndAssetMemoryCounters() {
    MemoryService::Get().SetSceneActorBudget(1000);
    Scene sc("MemScene");
    Actor* a = sc.CreateActor("A");
    Actor* b = sc.CreateActor("B");
    (void)a;
    (void)b;
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 2, "scene live actor count")) return false;
    sc.DestroyActor(a);
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 1, "scene count after DestroyActor")) return false;
    sc.Clear();
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 0, "scene count after Clear")) return false;

    AssetManager& am = AssetManager::Get();
    am.Clear();
    (void)am.GetCubeMesh();
    if (!Check(am.GetEstimatedAssetCpuBytes() > 0, "asset CPU estimate after builtin mesh")) return false;
    if (!Check(am.GetEstimatedAssetCpuBytesByType(AssetType::Mesh) > 0, "per-type mesh bucket")) return false;
    am.Clear();
    if (!Check(am.GetEstimatedAssetCpuBytes() == 0, "asset CPU zero after Clear")) return false;
    return true;
}

} // namespace

int main() {
    MemoryService::Get().Init();

    int failed = 0;

    if (!TestMemoryLinearAllocator()) { ++failed; }
    if (!TestMemoryPoolAllocator()) { ++failed; }
    if (!TestMemoryServiceHeapRoundTrip()) { ++failed; }
    if (!TestSceneAndAssetMemoryCounters()) { ++failed; }

    if (!TestSceneSerializationRegression()) { ++failed; }
    if (!TestTransformHierarchyWorldPosition()) { ++failed; }
    if (!TestInputBoundaries()) { ++failed; }
    if (!TestGamepadStateTransitions()) { ++failed; }
    if (!TestAssetFileImporters()) { ++failed; }

    Input::Shutdown();

    MemoryService::Get().Shutdown();

    if (failed == 0) {
        std::cout << "[PASS] All tests passed\n";
        return 0;
    }

    std::cerr << "[FAIL] Total failed suites: " << failed << '\n';
    return 1;
}
