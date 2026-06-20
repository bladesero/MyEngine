#pragma once

#include <cstdint>
#include <limits>
#include <string>

struct ActorHandle {
    static constexpr uint32_t InvalidIndex = (std::numeric_limits<uint32_t>::max)();

    uint32_t index = InvalidIndex;
    uint32_t generation = 0;

    bool IsValid() const { return index != InvalidIndex && generation != 0; }
    uint64_t ToUInt64() const { return (static_cast<uint64_t>(generation) << 32u) | index; }
    static ActorHandle FromUInt64(uint64_t value) {
        return {static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32u)};
    }
    explicit operator bool() const { return IsValid(); }
    friend bool operator==(ActorHandle a, ActorHandle b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(ActorHandle a, ActorHandle b) { return !(a == b); }
};

using ComponentTypeID = std::string;

struct ComponentHandle {
    ActorHandle actor;
    ComponentTypeID type;
    bool IsValid() const { return actor.IsValid() && !type.empty(); }
};

enum class ActorState : uint8_t {
    PendingCreate,
    Constructed,
    Active,
    Inactive,
    PendingDestroy,
    Destroyed,
};

enum class SceneState : uint8_t {
    Loading,
    Edit,
    Playing,
    Paused,
    Stopping,
};
