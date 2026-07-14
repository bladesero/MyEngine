#include "Editor/InspectorSectionShared.h"

namespace {
class PhysicsInspectorSection final : public ActorInspectorSection {
public:
    const char* GetID() const override { return "physics"; }
    int GetOrder() const override { return 300; }

    void Draw(EditorContext& context) override {
        Actor* actor = SelectedActor(context);
        if (!actor)
            return;

        auto* rb = actor->GetComponent<RigidBodyComponent>();
        auto* box = actor->GetComponent<BoxColliderComponent>();
        auto* sphere = actor->GetComponent<SphereColliderComponent>();
        auto* capsule = actor->GetComponent<CapsuleColliderComponent>();
        auto* character = actor->GetComponent<CharacterControllerComponent>();
        if (!rb && !box && !sphere && !capsule && !character)
            return;

        ImGui::Separator();
        ImGui::PushID("Physics");
        if (!SectionHeaderWithIcon(context, EditorIcons::Physics, "Physics")) {
            ImGui::PopID();
            return;
        }
        if (rb) {
            DrawEnabled(*rb);
            int bodyType = static_cast<int>(rb->GetBodyType());
            if (ImGui::Combo("Body Type", &bodyType, "Static\0Dynamic\0Kinematic\0")) {
                CommitComponentEdit(context, *actor, *rb, "bodyType",
                                    [&] { rb->SetBodyType(static_cast<BodyType>(bodyType)); });
            }
            float mass = rb->GetMass();
            if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.01f, 1000.0f)) {
                CommitComponentEdit(context, *actor, *rb, "mass", [&] { rb->SetMass(mass); });
            }
            float linearDamping = rb->GetLinearDamping(), angularDamping = rb->GetAngularDamping();
            if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *rb, "linearDamping",
                                    [&] { rb->SetLinearDamping(linearDamping); });
            }
            if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *rb, "angularDamping",
                                    [&] { rb->SetAngularDamping(angularDamping); });
            }
            float friction = rb->GetFriction(), restitution = rb->GetRestitution();
            if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "friction", [&] { rb->SetFriction(friction); });
            }
            if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "restitution", [&] { rb->SetRestitution(restitution); });
            }
            bool gravity = rb->UsesGravity();
            if (ImGui::Checkbox("Use Gravity", &gravity)) {
                CommitComponentEdit(context, *actor, *rb, "useGravity", [&] { rb->SetUseGravity(gravity); });
            }
            bool continuous = rb->GetCollisionDetectionMode() == CollisionDetectionMode::Continuous;
            if (ImGui::Checkbox("Continuous Collision", &continuous)) {
                CommitComponentEdit(context, *actor, *rb, "collisionDetection", [&] {
                    rb->SetCollisionDetectionMode(continuous ? CollisionDetectionMode::Continuous
                                                             : CollisionDetectionMode::Discrete);
                });
            }
            Vec3 velocity = rb->GetVelocity(), angularVelocity = rb->GetAngularVelocity();
            if (DrawVec3("Velocity", velocity, 0.05f)) {
                CommitComponentEdit(context, *actor, *rb, "velocity", [&] { rb->SetVelocity(velocity); });
            }
            if (DrawVec3("Angular Velocity", angularVelocity, 0.05f)) {
                CommitComponentEdit(context, *actor, *rb, "angularVelocity",
                                    [&] { rb->SetAngularVelocity(angularVelocity); });
            }
            Vec3 linearLocks = rb->GetLinearAxisLocks(), angularLocks = rb->GetAngularAxisLocks();
            if (DrawVec3("Linear Axis Locks", linearLocks, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "linearAxisLocks",
                                    [&] { rb->SetLinearAxisLocks(linearLocks); });
            }
            if (DrawVec3("Angular Axis Locks", angularLocks, 1.0f)) {
                CommitComponentEdit(context, *actor, *rb, "angularAxisLocks",
                                    [&] { rb->SetAngularAxisLocks(angularLocks); });
            }
            if (EditorWidgets::IconButton("RemoveRigidBody", "X", "Remove RigidBody"))
                RemoveComponentByType(context, *actor, "RigidBody");
        }

        const auto drawCollider = [&](ColliderComponent& collider) {
            bool trigger = collider.IsTrigger();
            if (ImGui::Checkbox("Trigger", &trigger)) {
                CommitComponentEdit(context, *actor, collider, "isTrigger", [&] { collider.SetTrigger(trigger); });
            }
            uint32_t layer = collider.GetLayer(), mask = collider.GetLayerMask();
            if (ImGui::InputScalar("Layer", ImGuiDataType_U32, &layer)) {
                CommitComponentEdit(context, *actor, collider, "layer", [&] { collider.SetLayer(layer); });
            }
            if (ImGui::InputScalar("Layer Mask", ImGuiDataType_U32, &mask)) {
                CommitComponentEdit(context, *actor, collider, "layerMask", [&] { collider.SetLayerMask(mask); });
            }
        };

        if (box) {
            ImGui::TextUnformatted("Box Collider");
            DrawEnabled(*box);
            Vec3 half = box->GetHalfExtents();
            if (DrawVec3("HalfExtents", half, 0.05f)) {
                CommitComponentEdit(context, *actor, *box, "halfExtents", [&] { box->SetHalfExtents(half); });
            }
            drawCollider(*box);
            if (EditorWidgets::IconButton("RemoveBoxCollider", "X", "Remove Box Collider"))
                RemoveComponentByType(context, *actor, "BoxCollider");
        }

        if (sphere) {
            ImGui::TextUnformatted("Sphere Collider");
            DrawEnabled(*sphere);
            float radius = sphere->GetRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *sphere, "radius", [&] { sphere->SetRadius(radius); });
            }
            drawCollider(*sphere);
            if (EditorWidgets::IconButton("RemoveSphereCollider", "X", "Remove Sphere Collider"))
                RemoveComponentByType(context, *actor, "SphereCollider");
        }
        if (capsule) {
            ImGui::TextUnformatted("Capsule Collider");
            DrawEnabled(*capsule);
            float radius = capsule->GetRadius(), halfHeight = capsule->GetHalfHeight();
            if (ImGui::DragFloat("Capsule Radius", &radius, 0.05f, 0.01f, 100.0f)) {
                CommitComponentEdit(context, *actor, *capsule, "radius", [&] { capsule->SetRadius(radius); });
            }
            if (ImGui::DragFloat("Capsule Half Height", &halfHeight, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *capsule, "halfHeight",
                                    [&] { capsule->SetHalfHeight(halfHeight); });
            }
            drawCollider(*capsule);
            if (EditorWidgets::IconButton("RemoveCapsuleCollider", "X", "Remove Capsule Collider"))
                RemoveComponentByType(context, *actor, "CapsuleCollider");
        }
        if (character) {
            ImGui::TextUnformatted("Character Controller");
            DrawEnabled(*character);
            bool gravity = character->UsesGravity();
            float step = character->GetStepOffset(), slope = character->GetMaxSlopeAngle();
            float jumpSpeed = character->GetJumpSpeed(), airControl = character->GetAirControl();
            if (ImGui::Checkbox("Character Gravity", &gravity)) {
                CommitComponentEdit(context, *actor, *character, "useGravity",
                                    [&] { character->SetUseGravity(gravity); });
            }
            if (ImGui::DragFloat("Step Offset", &step, 0.01f, 0.0f, 10.0f)) {
                CommitComponentEdit(context, *actor, *character, "stepOffset", [&] { character->SetStepOffset(step); });
            }
            if (ImGui::SliderFloat("Max Slope Angle", &slope, 0.0f, 89.0f)) {
                CommitComponentEdit(context, *actor, *character, "maxSlopeAngle",
                                    [&] { character->SetMaxSlopeAngle(slope); });
            }
            if (ImGui::DragFloat("Jump Speed", &jumpSpeed, 0.05f, 0.0f, 100.0f)) {
                CommitComponentEdit(context, *actor, *character, "jumpSpeed",
                                    [&] { character->SetJumpSpeed(jumpSpeed); });
            }
            if (ImGui::SliderFloat("Air Control", &airControl, 0.0f, 1.0f)) {
                CommitComponentEdit(context, *actor, *character, "airControl",
                                    [&] { character->SetAirControl(airControl); });
            }
            if (EditorWidgets::IconButton("RemoveCharacterController", "X", "Remove Character Controller"))
                RemoveComponentByType(context, *actor, "CharacterController");
        }
        ImGui::PopID();
    }
};

} // namespace

void RegisterPhysicsInspectorSections(std::vector<std::unique_ptr<EditorInspectorSection>>& sections) {
    sections.push_back(std::make_unique<PhysicsInspectorSection>());
}
