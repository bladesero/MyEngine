#include "Assets/AssetManager.h"
#include "Input/Input.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
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

} // namespace

int main() {
    int failed = 0;

    if (!TestSceneSerializationRegression()) { ++failed; }
    if (!TestTransformHierarchyWorldPosition()) { ++failed; }
    if (!TestInputBoundaries()) { ++failed; }

    if (failed == 0) {
        std::cout << "[PASS] All tests passed\n";
        return 0;
    }

    std::cerr << "[FAIL] Total failed suites: " << failed << '\n';
    return 1;
}
