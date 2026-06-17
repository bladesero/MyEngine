#include "Physics/PhysicsWorld.h"

#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/CollisionShapes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {

struct PhysicsEntry {
    enum class ShapeType { Box, Sphere, Capsule };
    Actor* actor = nullptr;
    RigidBodyComponent* body = nullptr;
    ColliderComponent* collider = nullptr;
    ShapeType shapeType = ShapeType::Box;
    AABB bounds;
};

AABB GetBounds(const PhysicsEntry& entry)
{
    if (entry.shapeType == PhysicsEntry::ShapeType::Box) {
        return static_cast<BoxColliderComponent*>(entry.collider)->GetWorldBounds();
    }
    if (entry.shapeType == PhysicsEntry::ShapeType::Sphere) {
        return static_cast<SphereColliderComponent*>(entry.collider)->GetWorldBounds();
    }
    return static_cast<CapsuleColliderComponent*>(entry.collider)->GetWorldBounds();
}

bool ComputeContact(const PhysicsEntry& a, const PhysicsEntry& b,
                    ContactManifold& contact)
{
    using Type = PhysicsEntry::ShapeType;
    if (a.shapeType == Type::Box && b.shapeType == Type::Box) {
        return Collide(
            static_cast<BoxColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<BoxColliderComponent*>(b.collider)->GetWorldShape(), contact);
    }
    if (a.shapeType == Type::Sphere && b.shapeType == Type::Sphere) {
        return Collide(
            static_cast<SphereColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<SphereColliderComponent*>(b.collider)->GetWorldShape(), contact);
    }
    if (a.shapeType == Type::Capsule && b.shapeType == Type::Capsule) {
        return Collide(
            static_cast<CapsuleColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<CapsuleColliderComponent*>(b.collider)->GetWorldShape(), contact);
    }

    bool reverse = false;
    bool hit = false;
    if (a.shapeType == Type::Sphere && b.shapeType == Type::Box) {
        hit = Collide(
            static_cast<SphereColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<BoxColliderComponent*>(b.collider)->GetWorldShape(), contact);
    } else if (a.shapeType == Type::Box && b.shapeType == Type::Sphere) {
        hit = Collide(
            static_cast<SphereColliderComponent*>(b.collider)->GetWorldShape(),
            static_cast<BoxColliderComponent*>(a.collider)->GetWorldShape(), contact);
        reverse = true;
    } else if (a.shapeType == Type::Capsule && b.shapeType == Type::Sphere) {
        hit = Collide(
            static_cast<CapsuleColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<SphereColliderComponent*>(b.collider)->GetWorldShape(), contact);
    } else if (a.shapeType == Type::Sphere && b.shapeType == Type::Capsule) {
        hit = Collide(
            static_cast<CapsuleColliderComponent*>(b.collider)->GetWorldShape(),
            static_cast<SphereColliderComponent*>(a.collider)->GetWorldShape(), contact);
        reverse = true;
    } else if (a.shapeType == Type::Capsule && b.shapeType == Type::Box) {
        hit = Collide(
            static_cast<CapsuleColliderComponent*>(a.collider)->GetWorldShape(),
            static_cast<BoxColliderComponent*>(b.collider)->GetWorldShape(), contact);
    } else if (a.shapeType == Type::Box && b.shapeType == Type::Capsule) {
        hit = Collide(
            static_cast<CapsuleColliderComponent*>(b.collider)->GetWorldShape(),
            static_cast<BoxColliderComponent*>(a.collider)->GetWorldShape(), contact);
        reverse = true;
    }
    if (hit && reverse) contact.normal *= -1.0f;
    return hit;
}

bool RaycastSphere(const Ray& ray, const SphereShape& sphere,
                   float maxDistance, float& distance, Vec3& normal)
{
    const Vec3 offset = ray.origin - sphere.center;
    const float b = offset.Dot(ray.direction);
    const float c = offset.Dot(offset) - sphere.radius * sphere.radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;
    distance = -b - std::sqrt(discriminant);
    if (distance < 0.0f) distance = -b + std::sqrt(discriminant);
    if (distance < 0.0f || distance > maxDistance) return false;
    normal = (ray.origin + ray.direction * distance - sphere.center).Normalized();
    return true;
}

bool RaycastBox(const Ray& ray, const OrientedBox& box,
                float maxDistance, float& distance, Vec3& normal)
{
    const Vec3 relative = ray.origin - box.center;
    float nearDistance = 0.0f;
    float farDistance = maxDistance;
    Vec3 nearNormal = Vec3::Up();
    for (size_t axis = 0; axis < 3; ++axis) {
        const float origin = relative.Dot(box.axes[axis]);
        const float direction = ray.direction.Dot(box.axes[axis]);
        const float extent = axis == 0 ? box.halfExtents.x
            : (axis == 1 ? box.halfExtents.y : box.halfExtents.z);
        if (std::fabs(direction) < 1e-8f) {
            if (origin < -extent || origin > extent) return false;
            continue;
        }
        float t0 = (-extent - origin) / direction;
        float t1 = (extent - origin) / direction;
        Vec3 axisNormal = box.axes[axis] * (direction > 0.0f ? -1.0f : 1.0f);
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > nearDistance) {
            nearDistance = t0;
            nearNormal = axisNormal;
        }
        farDistance = (std::min)(farDistance, t1);
        if (nearDistance > farDistance) return false;
    }
    distance = nearDistance;
    normal = nearNormal;
    return distance <= maxDistance;
}

bool RaycastCapsule(const Ray& ray, const CapsuleShape& capsule,
                    float maxDistance, float& distance, Vec3& normal)
{
    float tMin = 0.0f;
    float tMax = 0.0f;
    if (!ComputeBounds(capsule).IntersectRay(ray, tMin, tMax) ||
        tMin > maxDistance) return false;
    distance = tMin;
    const Vec3 point = ray.origin + ray.direction * distance;
    const Vec3 segment = capsule.pointB - capsule.pointA;
    const float denom = segment.LengthSq();
    const float t = denom > 1e-8f
        ? std::clamp((point - capsule.pointA).Dot(segment) / denom, 0.0f, 1.0f)
        : 0.0f;
    normal = (point - (capsule.pointA + segment * t)).Normalized();
    if (normal.LengthSq() < 1e-8f) normal = Vec3::Up();
    return true;
}

void DispatchEvent(Actor& actor, Actor& other, const ContactManifold& contact,
                   bool trigger, CollisionEventPhase phase, bool reverseNormal)
{
    CollisionEvent event;
    event.other = &other;
    event.point = contact.point;
    event.normal = reverseNormal ? contact.normal * -1.0f : contact.normal;
    event.depth = contact.depth;
    event.trigger = trigger;
    event.phase = phase;
    actor.ForEachComponent([&](Component& component) {
        if (component.IsEnabled()) component.OnCollisionEvent(event);
    });
}

} // namespace

void PhysicsWorld::Step(Scene& scene, float deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return;
    m_Accumulator += std::min(deltaSeconds, 0.25f);
    while (m_Accumulator >= m_FixedDelta) {
        Substep(scene, m_FixedDelta);
        m_Accumulator -= m_FixedDelta;
    }
}

void PhysicsWorld::Substep(Scene& scene, float deltaSeconds)
{
    scene.ForEach([&](Actor& actor) { actor.FixedUpdate(deltaSeconds); });

    std::vector<PhysicsEntry> entries;
    std::vector<std::pair<CharacterControllerComponent*, CapsuleColliderComponent*>> controllers;
    scene.ForEach([&](Actor& actor) {
        if (auto* controller = actor.GetComponent<CharacterControllerComponent>()) {
            auto* capsule = actor.GetComponent<CapsuleColliderComponent>();
            if (actor.IsActive() && controller->IsEnabled() &&
                capsule && capsule->IsEnabled()) {
                controllers.emplace_back(controller, capsule);
            }
        }
        auto* body = actor.GetComponent<RigidBodyComponent>();
        if (!actor.IsActive() || !body || !body->IsEnabled()) return;
        if (auto* collider = actor.GetComponent<BoxColliderComponent>();
            collider && collider->IsEnabled()) {
            entries.push_back({ &actor, body, collider, PhysicsEntry::ShapeType::Box, {} });
        } else if (auto* collider = actor.GetComponent<SphereColliderComponent>();
                   collider && collider->IsEnabled()) {
            entries.push_back({ &actor, body, collider, PhysicsEntry::ShapeType::Sphere, {} });
        } else if (auto* collider = actor.GetComponent<CapsuleColliderComponent>();
                   collider && collider->IsEnabled()) {
            entries.push_back({ &actor, body, collider, PhysicsEntry::ShapeType::Capsule, {} });
        }
    });

    for (PhysicsEntry& entry : entries) {
        if (!entry.body->IsDynamic() || entry.body->IsSleeping()) continue;
        Vec3 acceleration = entry.body->ConsumeForce() * entry.body->GetInverseMass();
        if (entry.body->UsesGravity()) acceleration += m_Gravity;

        Vec3 velocity = entry.body->GetVelocity() + acceleration * deltaSeconds;
        velocity *= 1.0f / (1.0f + entry.body->GetLinearDamping() * deltaSeconds);
        entry.body->SetVelocityFromPhysics(velocity);
        entry.actor->GetTransform().position += velocity * deltaSeconds;
    }

    for (PhysicsEntry& entry : entries) entry.bounds = GetBounds(entry);
    std::sort(entries.begin(), entries.end(), [](const PhysicsEntry& a, const PhysicsEntry& b) {
        return a.bounds.min.x < b.bounds.min.x;
    });

    std::map<std::pair<uint64_t, uint64_t>, bool> currentPairs;
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            PhysicsEntry& a = entries[i];
            PhysicsEntry& b = entries[j];
            if (b.bounds.min.x > a.bounds.max.x) break;
            if ((a.collider->GetLayerMask() & b.collider->GetLayer()) == 0 ||
                (b.collider->GetLayerMask() & a.collider->GetLayer()) == 0) {
                continue;
            }
            if (!a.bounds.Intersects(b.bounds)) continue;

            ContactManifold contact;
            if (!ComputeContact(a, b, contact)) continue;
            const bool trigger = a.collider->IsTrigger() || b.collider->IsTrigger();
            const std::pair<uint64_t, uint64_t> pairKey =
                a.actor->GetID() < b.actor->GetID()
                ? std::make_pair(a.actor->GetID(), b.actor->GetID())
                : std::make_pair(b.actor->GetID(), a.actor->GetID());
            currentPairs[pairKey] = trigger;
            const CollisionEventPhase phase = m_ActivePairs.count(pairKey)
                ? CollisionEventPhase::Stay : CollisionEventPhase::Enter;
            DispatchEvent(*a.actor, *b.actor, contact, trigger, phase, false);
            DispatchEvent(*b.actor, *a.actor, contact, trigger, phase, true);

            if (trigger) continue;
            const float invA = a.body->GetInverseMass();
            const float invB = b.body->GetInverseMass();
            const float invTotal = invA + invB;
            if (invTotal <= 0.0f) continue;

            const Vec3 correction = contact.normal * (contact.depth / invTotal);
            if (invA > 0.0f) a.actor->GetTransform().position += correction * invA;
            if (invB > 0.0f) b.actor->GetTransform().position -= correction * invB;

            const Vec3 relativeVelocity = a.body->GetVelocity() - b.body->GetVelocity();
            const float separatingVelocity = relativeVelocity.Dot(contact.normal);
            if (separatingVelocity >= 0.0f) continue;

            const float restitution = std::min(a.body->GetRestitution(), b.body->GetRestitution());
            const float impulseMagnitude = -(1.0f + restitution) * separatingVelocity / invTotal;
            const Vec3 impulse = contact.normal * impulseMagnitude;
            const bool wakeA = a.body->IsSleeping() &&
                b.body->GetVelocity().LengthSq() > 0.04f;
            const bool wakeB = b.body->IsSleeping() &&
                a.body->GetVelocity().LengthSq() > 0.04f;
            if (invA > 0.0f) {
                a.body->SetVelocityFromPhysics(a.body->GetVelocity() + impulse * invA);
                if (wakeA) a.body->WakeUp();
            }
            if (invB > 0.0f) {
                b.body->SetVelocityFromPhysics(b.body->GetVelocity() - impulse * invB);
                if (wakeB) b.body->WakeUp();
            }

            Vec3 postRelative = a.body->GetVelocity() - b.body->GetVelocity();
            Vec3 tangent = postRelative -
                contact.normal * postRelative.Dot(contact.normal);
            if (tangent.LengthSq() > 1e-8f) {
                tangent = tangent.Normalized();
                float tangentImpulse = -postRelative.Dot(tangent) / invTotal;
                const float friction = std::sqrt(
                    a.body->GetFriction() * b.body->GetFriction());
                const float maxFriction = impulseMagnitude * friction;
                tangentImpulse = std::clamp(
                    tangentImpulse, -maxFriction, maxFriction);
                const Vec3 frictionImpulse = tangent * tangentImpulse;
                if (invA > 0.0f) {
                    a.body->SetVelocityFromPhysics(
                        a.body->GetVelocity() + frictionImpulse * invA);
                }
                if (invB > 0.0f) {
                    b.body->SetVelocityFromPhysics(
                        b.body->GetVelocity() - frictionImpulse * invB);
                }
            }
        }
    }

    for (const auto& [pair, wasTrigger] : m_ActivePairs) {
        if (currentPairs.count(pair)) continue;
        Actor* a = scene.FindByID(pair.first);
        Actor* b = scene.FindByID(pair.second);
        if (!a || !b) continue;
        ContactManifold emptyContact;
        DispatchEvent(*a, *b, emptyContact, wasTrigger, CollisionEventPhase::Exit, false);
        DispatchEvent(*b, *a, emptyContact, wasTrigger, CollisionEventPhase::Exit, true);
    }
    m_ActivePairs = std::move(currentPairs);

    for (PhysicsEntry& entry : entries) {
        entry.body->UpdateSleep(deltaSeconds);
    }

    for (auto& [controller, capsule] : controllers) {
        Actor* actor = controller->GetOwner();
        if (!actor) continue;
        actor->GetTransform().position += controller->Integrate(deltaSeconds, m_Gravity);
        controller->SetGrounded(false);
        PhysicsEntry controllerEntry;
        controllerEntry.actor = actor;
        controllerEntry.collider = capsule;
        controllerEntry.shapeType = PhysicsEntry::ShapeType::Capsule;
        for (PhysicsEntry& other : entries) {
            if (other.actor == actor ||
                (capsule->GetLayerMask() & other.collider->GetLayer()) == 0 ||
                (other.collider->GetLayerMask() & capsule->GetLayer()) == 0) {
                continue;
            }
            controllerEntry.bounds = capsule->GetWorldBounds();
            if (!controllerEntry.bounds.Intersects(other.bounds)) continue;
            ContactManifold contact;
            if (!ComputeContact(controllerEntry, other, contact)) continue;
            if (capsule->IsTrigger() || other.collider->IsTrigger()) continue;
            actor->GetTransform().position += contact.normal * contact.depth;
            controller->ClipVelocity(contact.normal);
            if (contact.normal.y > 0.5f) controller->SetGrounded(true);
        }
    }
}

bool PhysicsWorld::Raycast(const Scene& scene, const Ray& ray, float maxDistance,
                           uint32_t layerMask, RaycastHit& hit) const
{
    if (maxDistance <= 0.0f || ray.direction.LengthSq() < 1e-8f) return false;
    const Ray normalizedRay{ ray.origin, ray.direction.Normalized() };
    bool found = false;
    float nearest = maxDistance;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        float distance = 0.0f;
        Vec3 normal;
        ColliderComponent* collider = nullptr;
        bool shapeHit = false;
        if (auto* box = actor.GetComponent<BoxColliderComponent>();
            box && box->IsEnabled()) {
            collider = box;
            shapeHit = RaycastBox(normalizedRay, box->GetWorldShape(), nearest,
                                  distance, normal);
        } else if (auto* sphere = actor.GetComponent<SphereColliderComponent>();
                   sphere && sphere->IsEnabled()) {
            collider = sphere;
            shapeHit = RaycastSphere(normalizedRay, sphere->GetWorldShape(), nearest,
                                     distance, normal);
        } else if (auto* capsule = actor.GetComponent<CapsuleColliderComponent>();
                   capsule && capsule->IsEnabled()) {
            collider = capsule;
            shapeHit = RaycastCapsule(normalizedRay, capsule->GetWorldShape(), nearest,
                                      distance, normal);
        }
        if (!collider || (layerMask & collider->GetLayer()) == 0 || !shapeHit) return;
        nearest = distance;
        found = true;
        hit.actor = &actor;
        hit.distance = distance;
        hit.point = normalizedRay.origin + normalizedRay.direction * distance;
        hit.normal = normal;
    });
    return found;
}
