#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include <cstdint>
#include <string>

class Scene;

enum class GameFlowState : uint8_t {
    Boot,
    MainMenu,
    Loading,
    Gameplay,
    Paused,
    GameOver,
};

enum class GameInputOwner : uint8_t {
    World,
    UI,
    System,
};

enum class GamePauseReason : uint32_t {
    None = 0,
    User = 1u << 0,
    Editor = 1u << 1,
    SystemModal = 1u << 2,
    WindowInactive = 1u << 3,
};

constexpr GamePauseReason operator|(GamePauseReason lhs, GamePauseReason rhs) {
    return static_cast<GamePauseReason>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

struct GameFlowSnapshot {
    GameFlowState state = GameFlowState::Boot;
    GameInputOwner inputOwner = GameInputOwner::System;
    uint32_t pauseReasons = 0;
    bool simulationPaused = true;
    bool audioPaused = false;
    bool modal = true;
};

class MYENGINE_RUNTIME_API GameFlowController {
public:
    ~GameFlowController();
    const GameFlowSnapshot& GetSnapshot() const { return m_Snapshot; }
    GameFlowState GetState() const { return m_Snapshot.state; }
    GameInputOwner GetInputOwner() const { return m_Snapshot.inputOwner; }
    bool IsPaused() const { return m_Snapshot.simulationPaused; }
    bool HasPauseReason(GamePauseReason reason) const;

    void EnterBoot(Scene* scene = nullptr);
    void EnterMainMenu(Scene* scene = nullptr);
    void EnterGameplay(Scene* scene = nullptr);
    void EnterGameOver(Scene* scene = nullptr);
    void BeginLoading(Scene* scene = nullptr);
    void FinishLoading(Scene* scene, bool success);

    void RequestPause(GamePauseReason reason, Scene* scene = nullptr);
    void ReleasePause(GamePauseReason reason, Scene* scene = nullptr);
    void ClearPauseReasons(Scene* scene = nullptr);

    static const char* StateName(GameFlowState state);

private:
    void SetBaseState(GameFlowState state, Scene* scene);
    void Recompute(Scene* scene);

    GameFlowSnapshot m_Snapshot;
    GameFlowState m_StateBeforeLoading = GameFlowState::Boot;
};
