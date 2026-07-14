#include "Game/GameFlowController.h"

#include "Audio/AudioEngine.h"
#include "Scene/Scene.h"
#include "Input/Input.h"

namespace {
GameFlowController* g_ActiveFlowController = nullptr;

uint32_t PauseBit(GamePauseReason reason)
{
    return static_cast<uint32_t>(reason);
}
}

GameFlowController::~GameFlowController()
{
    if (g_ActiveFlowController != this) return;
    g_ActiveFlowController = nullptr;
    Input::SetGameplayInputEnabled(true);
    AudioEngine::Get().SetPaused(false);
}

bool GameFlowController::HasPauseReason(GamePauseReason reason) const
{
    return (m_Snapshot.pauseReasons & PauseBit(reason)) != 0;
}

void GameFlowController::EnterBoot(Scene* scene) { SetBaseState(GameFlowState::Boot, scene); }
void GameFlowController::EnterMainMenu(Scene* scene) { SetBaseState(GameFlowState::MainMenu, scene); }
void GameFlowController::EnterGameplay(Scene* scene) { SetBaseState(GameFlowState::Gameplay, scene); }
void GameFlowController::EnterGameOver(Scene* scene) { SetBaseState(GameFlowState::GameOver, scene); }

void GameFlowController::BeginLoading(Scene* scene)
{
    if (m_Snapshot.state != GameFlowState::Loading)
        m_StateBeforeLoading = m_Snapshot.state;
    m_Snapshot.state = GameFlowState::Loading;
    Recompute(scene);
}

void GameFlowController::FinishLoading(Scene* scene, bool success)
{
    if (m_Snapshot.state != GameFlowState::Loading) return;
    m_Snapshot.state = success ? GameFlowState::Gameplay : m_StateBeforeLoading;
    if (m_Snapshot.state == GameFlowState::Paused && m_Snapshot.pauseReasons == 0)
        m_Snapshot.state = GameFlowState::Gameplay;
    Recompute(scene);
}

void GameFlowController::RequestPause(GamePauseReason reason, Scene* scene)
{
    if (reason == GamePauseReason::None) return;
    m_Snapshot.pauseReasons |= PauseBit(reason);
    Recompute(scene);
}

void GameFlowController::ReleasePause(GamePauseReason reason, Scene* scene)
{
    m_Snapshot.pauseReasons &= ~PauseBit(reason);
    Recompute(scene);
}

void GameFlowController::ClearPauseReasons(Scene* scene)
{
    m_Snapshot.pauseReasons = 0;
    Recompute(scene);
}

const char* GameFlowController::StateName(GameFlowState state)
{
    switch (state) {
    case GameFlowState::Boot: return "Boot";
    case GameFlowState::MainMenu: return "MainMenu";
    case GameFlowState::Loading: return "Loading";
    case GameFlowState::Gameplay: return "Gameplay";
    case GameFlowState::Paused: return "Paused";
    case GameFlowState::GameOver: return "GameOver";
    }
    return "Unknown";
}

void GameFlowController::SetBaseState(GameFlowState state, Scene* scene)
{
    m_Snapshot.state = state;
    if (state != GameFlowState::Gameplay && state != GameFlowState::Paused)
        m_Snapshot.pauseReasons = 0;
    Recompute(scene);
}

void GameFlowController::Recompute(Scene* scene)
{
    g_ActiveFlowController = this;
    const bool explicitPause = m_Snapshot.pauseReasons != 0;
    if (m_Snapshot.state == GameFlowState::Gameplay && explicitPause)
        m_Snapshot.state = GameFlowState::Paused;
    else if (m_Snapshot.state == GameFlowState::Paused && !explicitPause)
        m_Snapshot.state = GameFlowState::Gameplay;

    switch (m_Snapshot.state) {
    case GameFlowState::Gameplay:
        m_Snapshot.inputOwner = GameInputOwner::World;
        m_Snapshot.simulationPaused = false;
        m_Snapshot.audioPaused = false;
        m_Snapshot.modal = false;
        break;
    case GameFlowState::Paused:
        m_Snapshot.inputOwner = GameInputOwner::UI;
        m_Snapshot.simulationPaused = true;
        m_Snapshot.audioPaused = true;
        m_Snapshot.modal = true;
        break;
    case GameFlowState::Loading:
        m_Snapshot.inputOwner = GameInputOwner::System;
        m_Snapshot.simulationPaused = true;
        m_Snapshot.audioPaused = true;
        m_Snapshot.modal = true;
        break;
    case GameFlowState::Boot:
        m_Snapshot.inputOwner = GameInputOwner::System;
        m_Snapshot.simulationPaused = true;
        m_Snapshot.audioPaused = false;
        m_Snapshot.modal = true;
        break;
    case GameFlowState::MainMenu:
    case GameFlowState::GameOver:
        m_Snapshot.inputOwner = GameInputOwner::UI;
        m_Snapshot.simulationPaused = true;
        m_Snapshot.audioPaused = false;
        m_Snapshot.modal = true;
        break;
    }

    if (scene) {
        if (m_Snapshot.simulationPaused) scene->Pause();
        else scene->Resume();
    }
    Input::SetGameplayInputEnabled(m_Snapshot.inputOwner == GameInputOwner::World);
    AudioEngine::Get().SetPaused(m_Snapshot.audioPaused);
}
