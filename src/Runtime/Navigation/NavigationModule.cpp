#include "Navigation/NavigationModule.h"

#include "Navigation/NavigationWorld.h"
#include "Scene/SceneSubsystems.h"

#include <memory>

namespace {
std::unique_ptr<ISceneNavigationSubsystem> CreateNavigationWorld() {
    return std::make_unique<NavigationWorld>();
}
}

bool AttachNavigationSubsystem() {
    return RegisterSceneNavigationSubsystemFactory(&CreateNavigationWorld);
}
