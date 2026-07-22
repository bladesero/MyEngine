#include "TestHarness.h"

#include "DebugDraw/DebugDraw.h"
#include "DebugDraw/DebugDrawService.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {

bool NearlyEqualVec3(const Vec3& left, const Vec3& right, float epsilon = 1e-4f) {
    return NearlyEqual(left.x, right.x, epsilon) && NearlyEqual(left.y, right.y, epsilon) &&
           NearlyEqual(left.z, right.z, epsilon);
}

class UnknownCollider final : public ColliderComponent {
public:
    const char* GetTypeName() const override { return "UnknownCollider"; }
};

bool TestDebugDrawImmediateCommandsAndFrameSnapshots() {
    DebugDrawService& service = DebugDrawService::Get();
    service.Clear();
    Scene scene("DebugDrawScene");
    Scene otherScene("OtherDebugDrawScene");
    const Color lineColor{0.1f, 0.2f, 0.3f, 0.4f};
    const Color sphereColor{0.5f, 0.6f, 0.7f, 0.8f};

    if (!Check(DebugDraw::DrawLine(scene, {1.0f, 2.0f, 3.0f}, {4.0f, 6.0f, 8.0f}, lineColor, 10.0f,
                                   DebugDrawDepthMode::Always, DebugDrawViewMask::Authoring) &&
                   DebugDraw::DrawSphere(scene, {2.0f, 3.0f, 4.0f}, 2.5f, sphereColor, 10.0f) &&
                   DebugDraw::DrawBox(scene, {5.0f, 6.0f, 7.0f}, {1.0f, 2.0f, 3.0f}, Quat::Identity(),
                                      DebugDraw::kDefaultColor, 10.0f),
               "immediate DebugDraw entry point rejected valid geometry")) {
        return false;
    }

    const auto first = service.SnapshotForScene(scene, 100, 20.0f);
    const auto other = service.SnapshotForScene(otherScene, 100, 20.0f);
    if (!Check(first && first->size() == 3 && other && other->empty(),
               "DebugDraw frame snapshot did not isolate scene commands")) {
        return false;
    }

    const DebugDrawCommand& line = (*first)[0];
    const DebugDrawCommand& sphere = (*first)[1];
    const DebugDrawCommand& box = (*first)[2];
    if (!Check(line.geometry == DebugDrawGeometryKind::Line && line.depthMode == DebugDrawDepthMode::Always &&
                   line.viewMask == DebugDrawViewMask::Authoring &&
                   NearlyEqualVec3(line.transform.TransformPoint(Vec3::Zero()), {1.0f, 2.0f, 3.0f}) &&
                   NearlyEqualVec3(line.transform.TransformPoint(Vec3::Right()), {4.0f, 6.0f, 8.0f}) &&
                   NearlyEqual(line.color.r, lineColor.r) && NearlyEqual(line.color.a, lineColor.a) &&
                   sphere.geometry == DebugDrawGeometryKind::Sphere && sphere.viewMask == DebugDrawViewMask::All &&
                   DebugDrawViewMatches(sphere.viewMask, DebugDrawViewMask::Runtime) &&
                   !DebugDrawViewMatches(line.viewMask, DebugDrawViewMask::Runtime) &&
                   NearlyEqualVec3(sphere.transform.TransformPoint(Vec3::Zero()), {2.0f, 3.0f, 4.0f}) &&
                   NearlyEqual(sphere.transform.TransformDir(Vec3::Right()).Length(), 2.5f) &&
                   box.geometry == DebugDrawGeometryKind::Box &&
                   NearlyEqualVec3(box.transform.TransformPoint(Vec3::Zero()), {5.0f, 6.0f, 7.0f}) &&
                   NearlyEqual(box.transform.TransformDir(Vec3::Up()).Length(), 2.0f),
               "immediate DebugDraw entry point produced incorrect unified command data")) {
        return false;
    }

    const CapsuleShape capsule{{0.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}, 0.75f};
    if (!Check(DebugDraw::DrawCapsule(scene, capsule), "capsule submission after frame seal failed"))
        return false;
    const auto sameFrame = service.SnapshotForScene(scene, 100, 20.1f);
    if (!Check(sameFrame.get() == first.get() && sameFrame->size() == 3,
               "submission after frame seal leaked into an existing viewport snapshot")) {
        return false;
    }
    const auto nextFrame = service.SnapshotForScene(scene, 101, 20.1f);
    if (!Check(nextFrame && nextFrame->size() == 4 && nextFrame->back().geometry == DebugDrawGeometryKind::Capsule &&
                   NearlyEqual(nextFrame->back().shapeParameters.x, 0.75f) &&
                   NearlyEqual(nextFrame->back().shapeParameters.y, 2.0f),
               "deferred capsule command was not activated on the next frame")) {
        return false;
    }
    const auto afterSingleFrame = service.SnapshotForScene(scene, 102, 20.2f);
    if (!Check(afterSingleFrame && afterSingleFrame->size() == 3,
               "zero-duration command survived beyond exactly one visible render frame")) {
        return false;
    }
    const auto afterExpiry = service.SnapshotForScene(scene, 103, 30.0f);
    service.Clear();
    return Check(afterExpiry && afterExpiry->empty(), "timed DebugDraw commands did not expire from the snapshot");
}

bool TestDebugDrawProceduralMeshAndColliders() {
    DebugDrawService& service = DebugDrawService::Get();
    service.Clear();
    const std::vector<Vec3> positions = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };
    const DebugDrawMeshHandle mesh = DebugDraw::CreateMesh(positions, {0, 1, 2, 0, 2, 3});
    const std::vector<uint32_t> expectedEdges = {0, 1, 0, 2, 0, 3, 1, 2, 2, 3};
    if (!Check(mesh && mesh->GetLineIndices() == expectedEdges && !DebugDraw::CreateMesh(positions, {0, 1}) &&
                   !DebugDraw::CreateMesh(positions, {0, 1, 9}),
               "procedural DebugDraw mesh validation or deterministic edge generation failed")) {
        return false;
    }

    Scene scene("DebugColliderScene");
    Actor* actor = scene.CreateActor("Colliders");
    actor->GetTransform().position = {3.0f, 4.0f, 5.0f};
    actor->GetTransform().rotation = {10.0f, 20.0f, 30.0f};
    actor->GetTransform().scale = {2.0f, 1.5f, 0.75f};
    auto* box = actor->AddComponent<BoxColliderComponent>();
    auto* sphere = actor->AddComponent<SphereColliderComponent>();
    auto* capsule = actor->AddComponent<CapsuleColliderComponent>();
    auto* unknown = actor->AddComponent<UnknownCollider>();
    box->SetHalfExtents({1.0f, 2.0f, 3.0f});
    sphere->SetRadius(1.25f);
    capsule->SetRadius(0.4f);
    capsule->SetHalfHeight(1.75f);
    BoxColliderComponent orphan;

    if (!Check(DebugDraw::DrawMesh(scene, mesh, Mat4::Translation({8.0f, 9.0f, 10.0f})) &&
                   DebugDraw::DrawCollider(*box) && DebugDraw::DrawCollider(*sphere) &&
                   DebugDraw::DrawCollider(*capsule) && !DebugDraw::DrawCollider(*unknown) &&
                   !DebugDraw::DrawCollider(orphan),
               "DebugDraw mesh or collider entry point handled a valid/invalid owner incorrectly")) {
        return false;
    }

    const auto snapshot = service.SnapshotForScene(scene, 200, 5.0f);
    if (!Check(snapshot && snapshot->size() == 4 && (*snapshot)[0].geometry == DebugDrawGeometryKind::ProceduralMesh &&
                   (*snapshot)[1].geometry == DebugDrawGeometryKind::Box &&
                   (*snapshot)[2].geometry == DebugDrawGeometryKind::Sphere &&
                   (*snapshot)[3].geometry == DebugDrawGeometryKind::Capsule,
               "collider entry points did not normalize to expected wireframe command kinds")) {
        return false;
    }

    const OrientedBox worldBox = box->GetWorldShape();
    const SphereShape worldSphere = sphere->GetWorldShape();
    const CapsuleShape worldCapsule = capsule->GetWorldShape();
    const DebugDrawCommand& boxCommand = (*snapshot)[1];
    const DebugDrawCommand& sphereCommand = (*snapshot)[2];
    const DebugDrawCommand& capsuleCommand = (*snapshot)[3];
    const Vec3 capsuleCenter = (worldCapsule.pointA + worldCapsule.pointB) * 0.5f;
    const bool valid =
        NearlyEqualVec3(boxCommand.transform.TransformPoint(Vec3::Zero()), worldBox.center) &&
        NearlyEqualVec3(sphereCommand.transform.TransformPoint(Vec3::Zero()), worldSphere.center) &&
        NearlyEqual(sphereCommand.transform.TransformDir(Vec3::Right()).Length(), worldSphere.radius) &&
        NearlyEqualVec3(capsuleCommand.transform.TransformPoint(Vec3::Zero()), capsuleCenter) &&
        NearlyEqual(capsuleCommand.shapeParameters.x, worldCapsule.radius) &&
        NearlyEqual(capsuleCommand.shapeParameters.y, (worldCapsule.pointB - worldCapsule.pointA).Length() * 0.5f);
    service.Clear();
    return Check(valid, "collider DebugDraw command did not preserve its world-space shape");
}

bool TestDebugDrawThreadingCapacityAndSceneLifetime() {
    DebugDrawService& service = DebugDrawService::Get();
    service.Clear();
    const DebugDrawServiceStats baseline = service.GetStats();
    Scene scene("ThreadedDebugDrawScene");
    constexpr uint32_t kThreadCount = 4;
    constexpr uint32_t kCommandsPerThread = 256;
    std::atomic_uint32_t accepted{0};
    std::vector<std::thread> workers;
    for (uint32_t threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        workers.emplace_back([&scene, &accepted, threadIndex, kCommandsPerThread]() {
            for (uint32_t commandIndex = 0; commandIndex < kCommandsPerThread; ++commandIndex) {
                const float x = static_cast<float>(threadIndex * kCommandsPerThread + commandIndex);
                if (DebugDraw::DrawLine(scene, {x, 0.0f, 0.0f}, {x, 1.0f, 0.0f}))
                    accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();

    const auto threaded = service.SnapshotForScene(scene, 300, 1.0f);
    bool strictlyOrdered = threaded && !threaded->empty();
    for (size_t index = 1; threaded && index < threaded->size(); ++index)
        strictlyOrdered = strictlyOrdered && (*threaded)[index - 1].sequence < (*threaded)[index].sequence;
    if (!Check(accepted.load() == kThreadCount * kCommandsPerThread && threaded &&
                   threaded->size() == accepted.load() && strictlyOrdered,
               "multi-threaded DebugDraw submission lost, duplicated or reordered a command")) {
        return false;
    }

    service.Clear();
    DebugDrawCommand command;
    command.scene = scene.GetLifetimeToken();
    command.sceneGeneration = scene.GetLifetimeGeneration();
    command.geometry = DebugDrawGeometryKind::Line;
    command.durationSeconds = 10.0f;
    size_t capacityAccepted = 0;
    for (size_t index = 0; index <= DebugDrawService::kMaxPendingCommands; ++index)
        capacityAccepted += service.Submit(command) ? 1u : 0u;
    const DebugDrawServiceStats overflow = service.GetStats();
    if (!Check(capacityAccepted == DebugDrawService::kMaxPendingCommands &&
                   overflow.pending == DebugDrawService::kMaxPendingCommands &&
                   overflow.dropped == baseline.dropped + 1,
               "DebugDraw pending capacity did not reject and count overflow deterministically")) {
        return false;
    }
    service.SnapshotForScene(scene, 301, 2.0f);
    if (!Check(service.Submit(command), "DebugDraw pending queue did not reopen after frame activation"))
        return false;
    service.SnapshotForScene(scene, 302, 2.1f);
    const DebugDrawServiceStats activeOverflow = service.GetStats();
    if (!Check(activeOverflow.active == DebugDrawService::kMaxActiveCommands &&
                   activeOverflow.dropped == baseline.dropped + 2,
               "DebugDraw active capacity did not reject and count overflow")) {
        return false;
    }

    service.Clear();
    auto transient = std::make_unique<Scene>("TransientDebugDrawScene");
    DebugDraw::DrawSphere(*transient, Vec3::Zero(), 1.0f, DebugDraw::kDefaultColor, 10.0f);
    service.SnapshotForScene(*transient, 400, 3.0f);
    transient.reset();
    Scene surviving("SurvivingScene");
    service.SnapshotForScene(surviving, 401, 3.1f);
    const DebugDrawServiceStats afterDestroy = service.GetStats();
    service.Clear();
    return Check(afterDestroy.active == 0,
                 "DebugDraw command retained a destroyed EditorWorld/PlayWorld scene lifetime");
}

MYENGINE_REGISTER_TEST("DebugDraw", "TestDebugDrawImmediateCommandsAndFrameSnapshots",
                       TestDebugDrawImmediateCommandsAndFrameSnapshots);
MYENGINE_REGISTER_TEST("DebugDraw", "TestDebugDrawProceduralMeshAndColliders", TestDebugDrawProceduralMeshAndColliders);
MYENGINE_REGISTER_TEST("DebugDraw", "TestDebugDrawThreadingCapacityAndSceneLifetime",
                       TestDebugDrawThreadingCapacityAndSceneLifetime);

} // namespace
