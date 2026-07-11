#include "TestHarness.h"

#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/CollisionShapes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Scene.h"

#include <cmath>

namespace {

class CollisionProbeComponent final : public Component {
public:
    const char* GetTypeName() const override { return "CollisionProbe"; }
    void OnCollisionEvent(const CollisionEvent& event) override {
        if (event.phase == CollisionEventPhase::Enter) ++enters;
        else if (event.phase == CollisionEventPhase::Stay) ++stays;
        else if (event.phase == CollisionEventPhase::Exit) ++exits;
        lastWasTrigger = event.trigger;
    }
    int enters = 0;
    int stays = 0;
    int exits = 0;
    bool lastWasTrigger = false;
};

bool TestPhysicsGroundCollision() {
    Scene scene("PhysicsCase");

    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>();

    Actor* box = scene.CreateActor("Box");
    box->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* body = box->AddComponent<RigidBodyComponent>();
    body->SetRestitution(0.0f);
    box->AddComponent<BoxColliderComponent>();

    for (int i = 0; i < 180; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
    }

    if (!Check(box->GetTransform().position.y >= 0.49f,
               "dynamic box penetrated static ground")) return false;
    return Check(std::fabs(body->GetVelocity().y) < 0.2f,
                 "dynamic box did not settle on ground");
}

bool TestExtendedCollisionShapes() {
    OrientedBox box;
    box.center = Vec3::Zero();
    const float angle = 35.0f * kDeg2Rad;
    box.axes[0] = Vec3{ std::cos(angle), 0.0f, -std::sin(angle) };
    box.axes[1] = Vec3::Up();
    box.axes[2] = Vec3{ std::sin(angle), 0.0f, std::cos(angle) };

    SphereShape sphere;
    sphere.center = { 0.8f, 0.0f, 0.0f };
    sphere.radius = 0.5f;
    ContactManifold contact;
    if (!Check(Collide(sphere, box, contact) && contact.depth > 0.0f,
               "sphere/OBB narrow phase failed")) return false;

    CapsuleShape capsuleA;
    capsuleA.pointA = { 0.0f, -1.0f, 0.0f };
    capsuleA.pointB = { 0.0f, 1.0f, 0.0f };
    capsuleA.radius = 0.35f;
    CapsuleShape capsuleB = capsuleA;
    capsuleB.pointA.x = capsuleB.pointB.x = 0.5f;
    if (!Check(Collide(capsuleA, capsuleB, contact),
               "capsule/capsule narrow phase failed")) return false;

    Scene scene("SphereGround");
    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    ground->GetTransform().rotation = { 0.0f, 0.0f, 0.1f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    auto* groundCollider = ground->AddComponent<BoxColliderComponent>();
    groundCollider->SetHalfExtents({ 3.0f, 0.5f, 3.0f });

    Actor* ball = scene.CreateActor("Ball");
    ball->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* ballBody = ball->AddComponent<RigidBodyComponent>();
    auto* ballCollider = ball->AddComponent<SphereColliderComponent>();
    ballCollider->SetRadius(0.5f);
    for (int i = 0; i < 180; ++i) scene.OnUpdate(1.0f / 60.0f);
    return Check(ball->GetTransform().position.y > 0.35f &&
                 std::fabs(ballBody->GetVelocity().y) < 0.3f,
                 "sphere did not settle on rotated OBB ground");
}

bool TestPhysicsBroadPhaseTriggersAndSleep() {
    Scene scene("PhysicsFeatures");
    scene.GetPhysicsWorld().SetGravity(Vec3::Zero());

    Actor* trigger = scene.CreateActor("Trigger");
    auto* triggerBody = trigger->AddComponent<RigidBodyComponent>();
    triggerBody->SetBodyType(BodyType::Static);
    auto* triggerCollider = trigger->AddComponent<BoxColliderComponent>();
    triggerCollider->SetTrigger(true);
    triggerCollider->SetLayer(2);
    triggerCollider->SetLayerMask(4);

    Actor* mover = scene.CreateActor("Mover");
    auto* moverBody = mover->AddComponent<RigidBodyComponent>();
    moverBody->SetUseGravity(false);
    auto* moverCollider = mover->AddComponent<SphereColliderComponent>();
    moverCollider->SetLayer(4);
    moverCollider->SetLayerMask(2);
    auto* probe = mover->AddComponent<CollisionProbeComponent>();

    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->enters == 1 && probe->stays >= 1 && probe->lastWasTrigger,
               "trigger enter/stay events failed")) return false;
    if (!Check(NearlyEqual(mover->GetTransform().position.x, 0.0f),
               "trigger incorrectly applied positional correction")) return false;

    mover->GetTransform().position = { 10.0f, 0.0f, 0.0f };
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->exits == 1, "trigger exit event failed")) return false;

    moverCollider->SetLayerMask(0);
    mover->GetTransform().position = Vec3::Zero();
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->enters == 1, "layer mask did not filter collision pair")) return false;

    Scene sleepScene("Sleep");
    Actor* ground = sleepScene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    auto* groundShape = ground->AddComponent<BoxColliderComponent>();
    groundShape->SetHalfExtents({ 4.0f, 0.5f, 4.0f });

    Actor* bodyActor = sleepScene.CreateActor("SleepingBody");
    bodyActor->GetTransform().position = { 0.0f, 1.0f, 0.0f };
    auto* body = bodyActor->AddComponent<RigidBodyComponent>();
    body->SetFriction(1.0f);
    bodyActor->AddComponent<SphereColliderComponent>();
    for (int i = 0; i < 360; ++i) sleepScene.OnUpdate(1.0f / 60.0f);
    return Check(body->IsSleeping() && body->GetVelocity().LengthSq() < 1e-6f,
                 "resting rigid body did not enter sleep state (sleeping=" +
                 std::to_string(body->IsSleeping()) + ", speedSq=" +
                 std::to_string(body->GetVelocity().LengthSq()) + ", angularSpeedSq=" +
                 std::to_string(body->GetAngularVelocity().LengthSq()) + ")");
}

bool TestRaycastAndCharacterController() {
    Scene rayScene("Raycast");
    Actor* ignored = rayScene.CreateActor("IgnoredNear");
    ignored->GetTransform().position = { 0.0f, 0.0f, 2.0f };
    ignored->AddComponent<SphereColliderComponent>()->SetLayer(2);
    Actor* target = rayScene.CreateActor("Target");
    target->GetTransform().position = { 0.0f, 0.0f, 4.0f };
    target->AddComponent<SphereColliderComponent>()->SetLayer(4);

    RaycastHit hit;
    Ray ray;
    ray.origin = Vec3::Zero();
    ray.direction = Vec3::Forward();
    if (!Check(rayScene.GetPhysicsWorld().Raycast(rayScene, ray, 10.0f, 4, hit),
               "layer-filtered raycast missed target")) return false;
    if (!Check(hit.actor == target && NearlyEqual(hit.distance, 3.5f, 0.05f),
               "raycast did not return nearest permitted collider")) return false;

    Scene controllerScene("Controller");
    Actor* ground = controllerScene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 5.0f, 0.5f, 5.0f });

    Actor* player = controllerScene.CreateActor("Player");
    player->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* capsule = player->AddComponent<CapsuleColliderComponent>();
    capsule->SetRadius(0.5f);
    capsule->SetHalfHeight(0.5f);
    auto* controller = player->AddComponent<CharacterControllerComponent>();
    controller->Move({ 1.0f, 0.0f, 0.0f });
    for (int i = 0; i < 180; ++i) controllerScene.OnUpdate(1.0f / 60.0f);
    return Check(controller->IsGrounded() &&
                 player->GetTransform().position.x > 2.0f &&
                 player->GetTransform().position.y > 0.9f,
                 "character controller did not move and settle on ground (grounded=" +
                 std::to_string(controller->IsGrounded()) + ", position=" +
                 std::to_string(player->GetTransform().position.x) + "," +
                 std::to_string(player->GetTransform().position.y) + "," +
                 std::to_string(player->GetTransform().position.z) + ")");
}

bool TestCharacterControllerContactStability() {
    Scene scene("CharacterContactStability");

    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    ground->AddComponent<RigidBodyComponent>()->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 8.0f, 0.5f, 8.0f });

    Actor* wall = scene.CreateActor("Wall");
    wall->GetTransform().position = { 2.0f, 1.0f, 0.0f };
    wall->AddComponent<RigidBodyComponent>()->SetBodyType(BodyType::Static);
    wall->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 0.25f, 1.0f, 3.0f });

    Actor* player = scene.CreateActor("Player");
    player->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* capsule = player->AddComponent<CapsuleColliderComponent>();
    capsule->SetRadius(0.5f);
    capsule->SetHalfHeight(0.5f);
    auto* controller = player->AddComponent<CharacterControllerComponent>();

    for (int i = 0; i < 180; ++i) scene.OnUpdate(1.0f / 60.0f);
    if (!Check(controller->IsGrounded(), "character did not settle before stability sampling")) return false;

    const float settledY = player->GetTransform().position.y;
    float minimumY = settledY;
    float maximumY = settledY;
    int groundedFrames = 0;
    for (int i = 0; i < 180; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
        const float y = player->GetTransform().position.y;
        minimumY = (std::min)(minimumY, y);
        maximumY = (std::max)(maximumY, y);
        if (controller->IsGrounded()) ++groundedFrames;
    }
    if (!Check(maximumY - minimumY < 0.005f && groundedFrames == 180 &&
               std::abs(controller->GetActualVelocity().y) < 0.02f,
               "resting character contact jittered or lost grounded state")) return false;

    if (!Check(controller->Jump(), "grounded character rejected jump request")) return false;
    bool becameAirborne = false;
    bool landedAgain = false;
    for (int i = 0; i < 180; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
        becameAirborne = becameAirborne || !controller->IsGrounded();
        if (becameAirborne && controller->IsGrounded()) {
            landedAgain = true;
            break;
        }
    }
    if (!Check(becameAirborne && landedAgain,
               "character did not leave and reacquire stable ground after jumping")) return false;

    controller->Move({ 3.0f, 0.0f, 1.0f });
    float previousX = player->GetTransform().position.x;
    float maximumBacktrack = 0.0f;
    for (int i = 0; i < 120; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
        const float x = player->GetTransform().position.x;
        maximumBacktrack = (std::max)(maximumBacktrack, previousX - x);
        previousX = x;
    }
    return Check(player->GetTransform().position.x < 1.3f &&
                 player->GetTransform().position.z > 1.0f &&
                 maximumBacktrack < 0.01f && controller->IsGrounded(),
                 "character was unstable while sliding along a wall");
}

bool TestCharacterControllerDynamicBodyStability() {
    Scene scene("CharacterDynamicContact");

    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    ground->AddComponent<RigidBodyComponent>()->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 8.0f, 0.5f, 8.0f });

    Actor* box = scene.CreateActor("PushableBox");
    box->GetTransform().position = { 2.0f, 0.5f, 0.0f };
    auto* boxBody = box->AddComponent<RigidBodyComponent>();
    boxBody->SetMass(20.0f);
    boxBody->SetFriction(0.5f);
    box->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 0.5f, 0.5f, 0.5f });

    Actor* player = scene.CreateActor("Player");
    player->GetTransform().position = { 0.0f, 1.0f, 0.0f };
    auto* capsule = player->AddComponent<CapsuleColliderComponent>();
    capsule->SetRadius(0.5f);
    capsule->SetHalfHeight(0.5f);
    auto* controller = player->AddComponent<CharacterControllerComponent>();
    controller->Move({ 2.0f, 0.0f, 0.0f });

    int groundedFrames = 0;
    float maximumVerticalSpeed = 0.0f;
    for (int i = 0; i < 240; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
        if (i >= 60 && controller->IsGrounded()) ++groundedFrames;
        maximumVerticalSpeed = (std::max)(maximumVerticalSpeed,
                                          std::abs(controller->GetActualVelocity().y));
    }
    const Vec3 playerPosition = player->GetTransform().position;
    const Vec3 boxPosition = box->GetTransform().position;
    if (!Check(std::isfinite(playerPosition.x) && std::isfinite(playerPosition.y) &&
               std::isfinite(boxPosition.x) && std::isfinite(boxPosition.y) &&
               boxPosition.x > 2.1f && groundedFrames > 170 && maximumVerticalSpeed < 1.0f,
               "character interaction with a dynamic collider was unstable")) return false;

    Scene platformScene("CharacterMovingPlatform");
    Actor* platform = platformScene.CreateActor("Platform");
    platform->GetTransform().position = { 0.0f, 0.0f, 0.0f };
    auto* platformBody = platform->AddComponent<RigidBodyComponent>();
    platformBody->SetBodyType(BodyType::Kinematic);
    platformBody->SetUseGravity(false);
    platform->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 2.0f, 0.25f, 2.0f });

    Actor* rider = platformScene.CreateActor("Rider");
    rider->GetTransform().position = { 0.0f, 1.25f, 0.0f };
    auto* riderCapsule = rider->AddComponent<CapsuleColliderComponent>();
    riderCapsule->SetRadius(0.5f);
    riderCapsule->SetHalfHeight(0.5f);
    auto* riderController = rider->AddComponent<CharacterControllerComponent>();
    for (int i = 0; i < 30; ++i) platformScene.OnUpdate(1.0f / 60.0f);
    if (!Check(riderController->IsGrounded(), "character did not settle on moving platform")) return false;

    for (int i = 0; i < 120; ++i) {
        const Vec3 target = platform->GetWorldPosition() + Vec3(0.01f, 0.0f, 0.0f);
        platformBody->SetKinematicTarget(target, Vec3::Zero());
        platformScene.OnUpdate(1.0f / 60.0f);
    }
    return Check(riderController->IsGrounded() &&
                 platform->GetWorldPosition().x > 1.1f &&
                 rider->GetWorldPosition().x > 0.9f &&
                 std::abs(rider->GetWorldPosition().y - 1.25f) < 0.02f,
                 "character did not stably inherit moving platform velocity");
}

bool TestJoltRigidBodyExtensions() {
    Scene scene("JoltExtensions");
    scene.GetPhysicsWorld().SetGravity(Vec3::Zero());

    Actor* dynamic = scene.CreateActor("Dynamic");
    auto* body = dynamic->AddComponent<RigidBodyComponent>();
    body->SetUseGravity(false);
    body->SetLinearAxisLocks({1.0f, 0.0f, 0.0f});
    body->SetAngularAxisLocks({1.0f, 0.0f, 1.0f});
    body->SetCollisionDetectionMode(CollisionDetectionMode::Continuous);
    dynamic->AddComponent<BoxColliderComponent>();
    body->AddImpulse({10.0f, 2.0f, 0.0f});
    body->AddTorque({0.0f, 5.0f, 0.0f});
    for (int i = 0; i < 30; ++i) scene.OnUpdate(1.0f / 60.0f);
    if (!Check(std::fabs(dynamic->GetWorldPosition().x) < 0.01f &&
               dynamic->GetWorldPosition().y > 0.2f,
               "Jolt impulse or linear axis lock failed")) return false;
    if (!Check(std::fabs(body->GetAngularVelocity().y) > 0.05f,
               "Jolt torque did not produce angular velocity")) return false;

    Actor* kinematic = scene.CreateActor("Kinematic");
    auto* kinematicBody = kinematic->AddComponent<RigidBodyComponent>();
    kinematicBody->SetBodyType(BodyType::Kinematic);
    kinematicBody->SetUseGravity(false);
    kinematic->AddComponent<BoxColliderComponent>();
    kinematicBody->SetKinematicTarget({2.0f, 0.0f, 0.0f}, {0.0f, 45.0f, 0.0f});
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(NearlyEqual(kinematic->GetWorldPosition().x, 2.0f, 0.02f),
               "kinematic target was not applied")) return false;

    Actor* parent = scene.CreateActor("Parent");
    parent->GetTransform().position = {10.0f, 0.0f, 0.0f};
    Actor* child = scene.CreateActor("Child", parent);
    auto* childBody = child->AddComponent<RigidBodyComponent>();
    childBody->SetUseGravity(false);
    child->AddComponent<SphereColliderComponent>();
    childBody->Teleport({12.0f, 1.0f, 0.0f}, Vec3::Zero());
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(NearlyEqual(child->GetTransform().position.x, 2.0f, 0.02f),
               "world-space teleport was not converted to parent-local transform")) return false;

    childBody->SetEnabled(false);
    scene.OnUpdate(1.0f / 60.0f);
    childBody->SetEnabled(true);
    scene.OnUpdate(1.0f / 60.0f);
    return Check(NearlyEqual(child->GetWorldPosition().x, 12.0f, 0.02f),
                 "runtime rigid-body disable/enable lost its transform");
}

bool TestRigidBodyJsonCompatibility() {
    RigidBodyComponent source;
    source.SetBodyType(BodyType::Kinematic);
    source.SetAngularVelocity({1.0f, 2.0f, 3.0f});
    source.SetAngularDamping(0.7f);
    source.SetLinearAxisLocks({1.0f, 0.0f, 1.0f});
    source.SetAngularAxisLocks({0.0f, 1.0f, 0.0f});
    source.SetCollisionDetectionMode(CollisionDetectionMode::Continuous);
    nlohmann::json data;
    source.Serialize(data);
    RigidBodyComponent roundTrip;
    roundTrip.Deserialize(data);
    if (!Check(roundTrip.GetBodyType() == BodyType::Kinematic &&
               NearlyEqual(roundTrip.GetAngularVelocity().z, 3.0f) &&
               NearlyEqual(roundTrip.GetAngularDamping(), 0.7f) &&
               roundTrip.GetCollisionDetectionMode() == CollisionDetectionMode::Continuous,
               "new rigid-body fields failed JSON round trip")) return false;

    RigidBodyComponent legacy;
    legacy.Deserialize(nlohmann::json{{"bodyType", "static"}, {"mass", 4.0f},
                                     {"velocity", {1.0f, 0.0f, 0.0f}}});
    return Check(legacy.GetBodyType() == BodyType::Static &&
                 NearlyEqual(legacy.GetMass(), 4.0f) &&
                 legacy.GetCollisionDetectionMode() == CollisionDetectionMode::Discrete,
                 "legacy rigid-body JSON no longer loads with defaults");
}

MYENGINE_REGISTER_TEST("Physics", "TestPhysicsGroundCollision", TestPhysicsGroundCollision);
MYENGINE_REGISTER_TEST("Physics", "TestExtendedCollisionShapes", TestExtendedCollisionShapes);
MYENGINE_REGISTER_TEST("Physics", "TestPhysicsBroadPhaseTriggersAndSleep", TestPhysicsBroadPhaseTriggersAndSleep);
MYENGINE_REGISTER_TEST("Physics", "TestRaycastAndCharacterController", TestRaycastAndCharacterController);
MYENGINE_REGISTER_TEST("Physics", "TestCharacterControllerContactStability", TestCharacterControllerContactStability);
MYENGINE_REGISTER_TEST("Physics", "TestCharacterControllerDynamicBodyStability", TestCharacterControllerDynamicBodyStability);
MYENGINE_REGISTER_TEST("Physics", "TestJoltRigidBodyExtensions", TestJoltRigidBodyExtensions);
MYENGINE_REGISTER_TEST("Physics", "TestRigidBodyJsonCompatibility", TestRigidBodyJsonCompatibility);

} // namespace
