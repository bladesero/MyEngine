#include "Scene/SceneSubsystems.h"

#include <mutex>

namespace {
std::mutex g_FactoryMutex;
ScenePhysicsSubsystemFactory g_PhysicsFactory = nullptr;
SceneNavigationSubsystemFactory g_NavigationFactory = nullptr;
}

bool RegisterScenePhysicsSubsystemFactory(ScenePhysicsSubsystemFactory factory) {
    if (!factory)
        return false;
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    if (g_PhysicsFactory && g_PhysicsFactory != factory)
        return false;
    g_PhysicsFactory = factory;
    return true;
}

bool RegisterSceneNavigationSubsystemFactory(SceneNavigationSubsystemFactory factory) {
    if (!factory)
        return false;
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    if (g_NavigationFactory && g_NavigationFactory != factory)
        return false;
    g_NavigationFactory = factory;
    return true;
}

std::unique_ptr<IScenePhysicsSubsystem> CreateScenePhysicsSubsystem() {
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    return g_PhysicsFactory ? g_PhysicsFactory() : nullptr;
}

std::unique_ptr<ISceneNavigationSubsystem> CreateSceneNavigationSubsystem() {
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    return g_NavigationFactory ? g_NavigationFactory() : nullptr;
}

bool HasScenePhysicsSubsystemFactory() {
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    return g_PhysicsFactory != nullptr;
}

bool HasSceneNavigationSubsystemFactory() {
    std::lock_guard<std::mutex> lock(g_FactoryMutex);
    return g_NavigationFactory != nullptr;
}
