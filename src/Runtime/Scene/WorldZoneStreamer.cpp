#include "Scene/WorldZoneStreamer.h"

#include "Assets/AssetManager.h"
#include "Core/RuntimeFileSystem.h"
#include "Core/Memory/MemoryService.h"
#include "Core/TaskService.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

namespace {
struct ZoneLoadResult {
    bool success = false;
    SceneLoadPlan plan;
    std::string error;
};

bool IsSafeContentPath(const std::string& value)
{
    const std::filesystem::path path(value);
    if (value.rfind("Content/", 0) != 0 || path.is_absolute() || path.has_root_name())
        return false;
    for (const auto& part : path)
        if (part == "..") return false;
    return true;
}

float DistanceToBounds(const Vec3& point, const Vec3& center, const Vec3& halfExtents)
{
    const float dx = (std::max)(std::fabs(point.x - center.x) - (std::max)(0.0f, halfExtents.x), 0.0f);
    const float dy = (std::max)(std::fabs(point.y - center.y) - (std::max)(0.0f, halfExtents.y), 0.0f);
    const float dz = (std::max)(std::fabs(point.z - center.z) - (std::max)(0.0f, halfExtents.z), 0.0f);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}

struct WorldZoneStreamer::Impl {
    struct Entry {
        WorldZoneStreamDescriptor descriptor;
        WorldZoneStreamState state = WorldZoneStreamState::Unloaded;
        bool portalOpen = false;
        bool desired = false;
        WorldZoneID zoneID = 0;
        TaskHandle<ZoneLoadResult> loadTask;
        SceneLoadPlan plan;
        SceneInstantiationState instantiation;
        size_t instantiatedActors = 0;
        uint64_t lifetimeGeneration = 0;
        std::string lastError;
    };

    Vec3 observer = Vec3::Zero();
    size_t maxActorsPerFrame = 16;
    size_t maxTransitionsPerFrame = 2;
    size_t instantiatedThisFrame = 0;
    uint64_t actorBudgetBlockedFrames = 0;
    std::unordered_map<std::string, Entry> entries;

    bool DistanceDesired(const Entry& entry) const {
        const bool active = entry.state != WorldZoneStreamState::Unloaded &&
            entry.state != WorldZoneStreamState::Failed;
        const float threshold = active ? entry.descriptor.unloadDistance
                                       : entry.descriptor.loadDistance;
        return DistanceToBounds(observer, entry.descriptor.boundsCenter,
                                entry.descriptor.boundsHalfExtents) <= threshold;
    }
    bool Desired(const Entry& entry) const {
        const bool distance = DistanceDesired(entry);
        switch (entry.descriptor.triggerMode) {
        case WorldZoneTriggerMode::Distance: return distance;
        case WorldZoneTriggerMode::Portal: return entry.portalOpen;
        case WorldZoneTriggerMode::DistanceOrPortal: return distance || entry.portalOpen;
        case WorldZoneTriggerMode::DistanceAndPortal: return distance && entry.portalOpen;
        }
        return false;
    }
};

WorldZoneStreamer::WorldZoneStreamer() : m_Impl(std::make_unique<Impl>()) {}
WorldZoneStreamer::~WorldZoneStreamer() = default;

bool WorldZoneStreamer::Register(WorldZoneStreamDescriptor descriptor, std::string* error)
{
    if (descriptor.stableName.empty() || !IsSafeContentPath(descriptor.sourcePath)) {
        if (error) *error = "zone requires a stable name and safe Content path";
        return false;
    }
    if (descriptor.loadDistance < 0.0f ||
        descriptor.unloadDistance < descriptor.loadDistance) {
        if (error) *error = "zone unload distance must be greater than or equal to load distance";
        return false;
    }
    if (m_Impl->entries.count(descriptor.stableName)) {
        if (error) *error = "duplicate streamed zone: " + descriptor.stableName;
        return false;
    }
    Impl::Entry entry;
    entry.portalOpen = descriptor.portalInitiallyOpen;
    entry.descriptor = std::move(descriptor);
    m_Impl->entries.emplace(entry.descriptor.stableName, std::move(entry));
    if (error) error->clear();
    return true;
}

bool WorldZoneStreamer::Unregister(Scene& scene, const std::string& stableName)
{
    const auto found = m_Impl->entries.find(stableName);
    if (found == m_Impl->entries.end()) return false;
    if (found->second.zoneID) scene.DestroyZone(found->second.zoneID);
    m_Impl->entries.erase(found);
    return true;
}

bool WorldZoneStreamer::SetPortalOpen(const std::string& stableName, bool open)
{
    const auto found = m_Impl->entries.find(stableName);
    if (found == m_Impl->entries.end()) return false;
    found->second.portalOpen = open;
    return true;
}

bool WorldZoneStreamer::Retry(const std::string& stableName)
{
    const auto found = m_Impl->entries.find(stableName);
    if (found == m_Impl->entries.end() ||
        found->second.state != WorldZoneStreamState::Failed) return false;
    found->second.state = WorldZoneStreamState::Unloaded;
    found->second.lastError.clear();
    return true;
}

void WorldZoneStreamer::SetObserverPosition(const Vec3& position) { m_Impl->observer = position; }
const Vec3& WorldZoneStreamer::GetObserverPosition() const { return m_Impl->observer; }

void WorldZoneStreamer::SetBudgets(size_t maxActorsPerFrame, size_t maxTransitionsPerFrame)
{
    m_Impl->maxActorsPerFrame = (std::max)(size_t{1}, maxActorsPerFrame);
    m_Impl->maxTransitionsPerFrame = (std::max)(size_t{1}, maxTransitionsPerFrame);
}

void WorldZoneStreamer::Tick(Scene& scene, float deltaSeconds)
{
    (void)deltaSeconds;
    m_Impl->instantiatedThisFrame = 0;
    std::vector<Impl::Entry*> ordered;
    ordered.reserve(m_Impl->entries.size());
    for (auto& pair : m_Impl->entries) {
        pair.second.desired = m_Impl->Desired(pair.second);
        ordered.push_back(&pair.second);
    }
    std::sort(ordered.begin(), ordered.end(), [this](const auto* left, const auto* right) {
        if (left->descriptor.priority != right->descriptor.priority)
            return left->descriptor.priority > right->descriptor.priority;
        const float ld = DistanceToBounds(m_Impl->observer, left->descriptor.boundsCenter,
                                          left->descriptor.boundsHalfExtents);
        const float rd = DistanceToBounds(m_Impl->observer, right->descriptor.boundsCenter,
                                          right->descriptor.boundsHalfExtents);
        if (ld != rd) return ld < rd;
        return left->descriptor.stableName < right->descriptor.stableName;
    });

    size_t transitions = 0;
    for (Impl::Entry* entry : ordered) {
        if (transitions >= m_Impl->maxTransitionsPerFrame) break;
        if (entry->desired || entry->state == WorldZoneStreamState::Unloaded) continue;
        if (entry->zoneID) scene.DestroyZone(entry->zoneID);
        entry->zoneID = 0;
        entry->loadTask = {};
        entry->plan = {};
        entry->instantiation = {};
        entry->instantiatedActors = 0;
        entry->lifetimeGeneration = 0;
        entry->lastError.clear();
        entry->state = WorldZoneStreamState::Unloaded;
        ++transitions;
    }
    for (Impl::Entry* entry : ordered) {
        if (transitions >= m_Impl->maxTransitionsPerFrame) break;
        if (!entry->desired || entry->state != WorldZoneStreamState::Unloaded) continue;
        entry->zoneID = scene.CreateZone(entry->descriptor.stableName);
        if (!entry->zoneID) {
            entry->state = WorldZoneStreamState::Failed;
            entry->lastError = "failed to create world zone";
            continue;
        }
        entry->lifetimeGeneration = scene.GetZoneLifetimeToken(entry->zoneID).GetGeneration();
        const std::string sourcePath = entry->descriptor.sourcePath;
        entry->loadTask = scene.SubmitZoneTask(
            entry->zoneID,
            {"zone.read_parse." + entry->descriptor.stableName, TaskPriority::Normal},
            [sourcePath](CancellationToken cancellation, WorldZoneLifetimeToken lifetime) {
                ZoneLoadResult result;
                cancellation.ThrowIfCancellationRequested();
                if (!lifetime.IsAlive()) throw TaskCancelled();
                std::string source;
                if (!RuntimeFileSystem::Get().ReadText(sourcePath, source, &result.error)) {
                    const std::filesystem::path resolved = AssetManager::Get().ResolvePath(sourcePath);
                    RuntimeFileSystem::Get().ReadText(resolved.string(), source, &result.error);
                }
                cancellation.ThrowIfCancellationRequested();
                if (source.empty()) {
                    if (result.error.empty()) result.error = "zone source is empty";
                    return result;
                }
                result.success = SceneSerializer::BuildLoadPlan(source, result.plan, &result.error);
                cancellation.ThrowIfCancellationRequested();
                return result;
            });
        entry->state = WorldZoneStreamState::Loading;
        ++transitions;
    }

    size_t remainingActors = m_Impl->maxActorsPerFrame;
    const uint64_t actorBudget=MemoryService::Get().GetSceneActorBudget();
    if(actorBudget){
        const uint64_t live=MemoryService::Get().GetSceneLiveActorCount();
        remainingActors=static_cast<size_t>(std::min<uint64_t>(remainingActors,
            live<actorBudget?actorBudget-live:0));
        if(remainingActors==0)++m_Impl->actorBudgetBlockedFrames;
    }
    for (Impl::Entry* entry : ordered) {
        if (!entry->desired) continue;
        if (entry->state == WorldZoneStreamState::Loading && entry->loadTask.IsReady()) {
            try {
                ZoneLoadResult result = entry->loadTask.Get();
                entry->loadTask = {};
                if (!result.success) throw std::runtime_error(result.error);
                entry->plan = std::move(result.plan);
                for (const std::string& dependency : entry->plan.assetDependencies)
                    if (!scene.PinAssetToZone(entry->zoneID, dependency))
                        throw std::runtime_error("failed to pin zone dependency: " + dependency);
                entry->state = WorldZoneStreamState::Instantiating;
            } catch (const std::exception& exception) {
                entry->lastError = exception.what();
                if (entry->zoneID) scene.DestroyZone(entry->zoneID);
                entry->zoneID = 0;
                entry->state = WorldZoneStreamState::Failed;
                continue;
            }
        }
        if (entry->state != WorldZoneStreamState::Instantiating || remainingActors == 0) continue;
        std::vector<ActorHandle> created;
        bool complete = false;
        const size_t before = entry->instantiation.nextActor;
        std::string instantiateError;
        const bool instantiated = SceneSerializer::InstantiateLoadPlanAdditive(
            scene, entry->plan, entry->instantiation, remainingActors,
            created, complete, &instantiateError);
        bool ownershipTransferred = true;
        for (ActorHandle actor : created) {
            if (scene.TryGetActor(actor) && !scene.AssignActorToZone(entry->zoneID, actor))
                ownershipTransferred = false;
        }
        if (!instantiated || !ownershipTransferred) {
            if (instantiateError.empty())
                instantiateError = "failed to transfer additive actor ownership";
            entry->lastError = instantiateError;
            if (entry->zoneID) scene.DestroyZone(entry->zoneID);
            entry->zoneID = 0;
            entry->state = WorldZoneStreamState::Failed;
            continue;
        }
        const size_t count = entry->instantiation.nextActor - before;
        entry->instantiatedActors += count;
        m_Impl->instantiatedThisFrame += count;
        remainingActors -= (std::min)(remainingActors, count);
        if (complete) entry->state = WorldZoneStreamState::Loaded;
    }
}

void WorldZoneStreamer::Reset(Scene& scene)
{
    m_Impl->actorBudgetBlockedFrames=0;
    for (auto& pair : m_Impl->entries) {
        auto& entry = pair.second;
        if (entry.zoneID) scene.DestroyZone(entry.zoneID);
        entry.zoneID = 0;
        entry.loadTask = {};
        entry.plan = {};
        entry.instantiation = {};
        entry.instantiatedActors = 0;
        entry.lifetimeGeneration = 0;
        entry.desired = false;
        entry.state = WorldZoneStreamState::Unloaded;
        entry.lastError.clear();
    }
}

WorldZoneStreamerStats WorldZoneStreamer::GetStats() const
{
    WorldZoneStreamerStats result;
    result.registeredZones = m_Impl->entries.size();
    result.instantiatedActorsThisFrame = m_Impl->instantiatedThisFrame;
    result.actorBudgetBlockedFrames=m_Impl->actorBudgetBlockedFrames;
    for (const auto& pair : m_Impl->entries) {
        switch (pair.second.state) {
        case WorldZoneStreamState::Loaded: ++result.loadedZones; break;
        case WorldZoneStreamState::Loading:
        case WorldZoneStreamState::Instantiating: ++result.loadingZones; break;
        case WorldZoneStreamState::Failed: ++result.failedZones; break;
        default: break;
        }
    }
    return result;
}

std::vector<WorldZoneStreamEntryStats> WorldZoneStreamer::GetEntryStats() const
{
    std::vector<WorldZoneStreamEntryStats> result;
    result.reserve(m_Impl->entries.size());
    for (const auto& pair : m_Impl->entries) {
        const auto& entry = pair.second;
        result.push_back({entry.descriptor.stableName, entry.state, entry.desired,
                          entry.lifetimeGeneration, entry.instantiatedActors, entry.lastError});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.stableName < right.stableName;
    });
    return result;
}
