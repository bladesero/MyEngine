#include "Physics/PhysicsModule.h"

#include "Physics/PhysicsWorld.h"
#include "Scene/SceneSubsystems.h"

#include <memory>

namespace {
std::unique_ptr<IScenePhysicsSubsystem> CreatePhysicsWorld() {
    return std::make_unique<PhysicsWorld>();
}
} // namespace

bool AttachPhysicsSubsystem() {
    return RegisterScenePhysicsSubsystemFactory(&CreatePhysicsWorld);
}
