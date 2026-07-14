#include "Game/SceneRenderLayer.h"

#include "Core/Event.h"
#include "Core/Logger.h"
#include "Core/RuntimeFileSystem.h"
#include "Audio/AudioEngine.h"
#include "Game/DefaultSceneFactory.h"
#include "Project/RuntimeUserSettings.h"
#include "UI/Core/RuntimeUIScreenConfig.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/RHI/GpuSwapChain.h"
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cmath>
#include <vector>

SceneRenderLayer::SceneRenderLayer(IRenderContext* context, int viewportWidth, int viewportHeight)
    : SceneLayer("SceneRenderLayer"), m_RenderContext(context), m_Viewport(context, context, context),
      m_GameViewport(context, context, context) {
    m_Viewport.Initialize(viewportWidth, viewportHeight);
    m_GameViewport.Initialize(viewportWidth, viewportHeight);
}

void SceneRenderLayer::SetPresentEnabled(bool enabled) {
    m_PresentEnabled = enabled;
}

void SceneRenderLayer::SetSceneViewportUsesSimulationScene(bool enabled) {
    m_SceneViewportUsesSimulationScene = enabled;
}

void SceneRenderLayer::SetRenderPath(RenderPath path) {
    m_Viewport.SetRenderPath(path);
    m_GameViewport.SetRenderPath(path);
}

RenderPath SceneRenderLayer::GetRenderPath() const {
    return m_Viewport.GetRenderPath();
}

Scene& SceneRenderLayer::GetSceneViewportRenderScene() {
    return m_SceneViewportUsesSimulationScene && HasPlayWorld() ? GetSimulationScene() : GetEditorScene();
}

const Scene& SceneRenderLayer::GetSceneViewportRenderScene() const {
    return m_SceneViewportUsesSimulationScene && HasPlayWorld() ? GetSimulationScene() : GetEditorScene();
}

void SceneRenderLayer::OnAttach() {
    SceneLayer::OnAttach();
    int x = 0, y = 0, w = 0, h = 0;
    m_GameViewport.GetViewportRect(x, y, w, h);
    m_UIInputViewport = {x, y, w, h, 1.0f, 1.0f, true, true};
    m_UISystem.Resize(w, h);
    if (m_RenderContext) {
        m_UISystem.Initialize(m_RenderContext, m_RenderContext);
    }
    m_Viewport.GetViewportRect(x, y, w, h);
    Logger::Info("[SceneRenderLayer] attached (", w, "x", h, ")");
}

void SceneRenderLayer::OnDetach() {
    m_UISystem.Shutdown();
    SceneLayer::OnDetach();
}

void SceneRenderLayer::OnUpdate(float dt) {
    SceneLayer::OnUpdate(dt);
    if (m_ResourceBudgetEnabled)
        m_ResourceBudget.Tick();
    SyncSceneLoadOverlay();
    SyncRuntimeScreen();
    m_Viewport.OnUpdate(dt);
    m_UISystem.Update(GetSimulationScene(), dt);
}

void SceneRenderLayer::OnEvent(Event& event) {
    SceneLayer::OnEvent(event);
    const SceneLoadState loadState = GetSceneManager().GetState();
    if (event.type == EventType::KeyDown && !event.key.repeat) {
        if (GetSceneManager().IsLoading() && event.key.scancode == SDL_SCANCODE_ESCAPE) {
            GetSceneManager().CancelLoad();
            event.handled = true;
            SyncSceneLoadOverlay();
            return;
        }
        if (loadState == SceneLoadState::Failed) {
            if (event.key.scancode == SDL_SCANCODE_R || event.key.scancode == SDL_SCANCODE_RETURN) {
                GetSceneManager().RetryLastLoad();
                event.handled = true;
                SyncSceneLoadOverlay();
                return;
            }
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                GetSceneManager().DismissFailure();
                event.handled = true;
                SyncSceneLoadOverlay();
                return;
            }
        }
    }
    if (m_RuntimeScreensEnabled && !event.handled) {
        SyncRuntimeScreen();
        if (m_RuntimeScreens.Empty() && event.type == EventType::KeyDown && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_ESCAPE &&
            GetGameFlowController().GetState() == GameFlowState::Gameplay) {
            GetGameFlowController().RequestPause(GamePauseReason::User, &GetSimulationScene());
            SyncRuntimeScreen();
            event.handled = true;
            return;
        }
        if (!m_RuntimeScreens.Empty()) {
            size_t pointerIndex = 0;
            bool activate = false;
            if (m_UISystem.ProcessRuntimeScreenPointer(event, m_UIInputViewport, pointerIndex, activate)) {
                m_RuntimeScreens.SetFocusedIndex(pointerIndex);
                if (activate)
                    HandleRuntimeScreenEvent(m_RuntimeScreens.Activate(pointerIndex));
                SyncRuntimeScreen();
                return;
            }
            const RuntimeUIStackEvent screenEvent = m_RuntimeScreens.ProcessEvent(event);
            if (screenEvent.type != RuntimeUIStackEventType::None) {
                HandleRuntimeScreenEvent(screenEvent);
                SyncRuntimeScreen();
                event.handled = true;
                return;
            }
            if (m_RuntimeScreens.IsModal() &&
                (event.type == EventType::KeyDown || event.type == EventType::KeyUp ||
                 event.type == EventType::MouseButtonDown || event.type == EventType::MouseButtonUp ||
                 event.type == EventType::MouseMove || event.type == EventType::MouseWheel ||
                 event.type == EventType::GamepadButtonDown || event.type == EventType::GamepadButtonUp ||
                 event.type == EventType::GamepadAxisMotion)) {
                event.handled = true;
                return;
            }
        }
    }
    m_UISystem.ProcessEvent(GetSimulationScene(), event, m_UIInputViewport);
    if (event.handled)
        return;
    if (event.type == EventType::WindowResize) {
        const int windowW = event.resize.width;
        const int windowH = event.resize.height;
        if (windowW <= 0 || windowH <= 0)
            return;
        m_Viewport.OnWindowResize(windowW, windowH);
        m_GameViewport.OnWindowResize(windowW, windowH);
        if (m_PresentEnabled) {
            m_UIInputViewport = {0, 0, windowW, windowH, 1.0f, 1.0f, true, true};
            m_UISystem.Resize(windowW, windowH);
        }
        if (m_RenderContext) {
            if (GpuSwapChain* swapChain = m_RenderContext->GetSwapChain()) {
                m_GameViewport.ReleaseFrameResources();
                swapChain->Resize(static_cast<uint32_t>(windowW), static_cast<uint32_t>(windowH));
            }
        }
    }
}

void SceneRenderLayer::SetRuntimeScreensEnabled(bool enabled) {
    m_RuntimeScreensEnabled = enabled;
    if (!enabled)
        m_RuntimeScreens.Clear();
    SyncRuntimeScreen();
}

bool SceneRenderLayer::LoadRuntimeScreenConfig(const std::string& path, std::string* error) {
    std::string text;
    if (!RuntimeFileSystem::Get().ReadText(path, text, error))
        return false;
    RuntimeUIScreenConfig config;
    if (!RuntimeUIScreenConfig::FromText(text, config, error))
        return false;
    if (!config.Apply(m_RuntimeScreens, error))
        return false;
    SyncRuntimeScreen();
    return true;
}

void SceneRenderLayer::SyncRuntimeScreen() {
    if (!m_RuntimeScreensEnabled) {
        m_UISystem.SetRuntimeScreen({});
        return;
    }
    std::string desiredRoot;
    switch (GetGameFlowController().GetState()) {
    case GameFlowState::Paused:
        desiredRoot = "pause";
        break;
    case GameFlowState::MainMenu:
        desiredRoot = "mainMenu";
        break;
    case GameFlowState::GameOver:
        desiredRoot = "gameOver";
        break;
    default:
        break;
    }
    if (desiredRoot.empty()) {
        m_RuntimeScreens.Clear();
    } else if (m_RuntimeScreens.RootName() != desiredRoot) {
        m_RuntimeScreens.ReplaceRoot(desiredRoot);
    }
    RefreshRuntimeSettingsLabels();
    m_UISystem.SetRuntimeScreen(m_RuntimeScreens.GetView());
}

void SceneRenderLayer::HandleRuntimeScreenEvent(const RuntimeUIStackEvent& event) {
    if (event.type == RuntimeUIStackEventType::None || event.type == RuntimeUIStackEventType::FocusChanged)
        return;
    Scene& scene = GetSimulationScene();
    if (event.type == RuntimeUIStackEventType::Dismissed) {
        if (event.screen == "pause")
            GetGameFlowController().ReleasePause(GamePauseReason::User, &scene);
        return;
    }
    if (event.action == "settings") {
        m_RuntimeScreens.Push("settings");
    } else if (event.action == "back") {
        m_RuntimeScreens.Pop();
    } else if (event.action == "resume") {
        GetGameFlowController().ReleasePause(GamePauseReason::User, &scene);
    } else if (event.action == "mainMenu") {
        GetGameFlowController().EnterMainMenu(&scene);
    } else if (event.action == "retry") {
        const std::string path = GetSceneManager().GetRequestedPath();
        if (!path.empty())
            RequestSceneLoad(path);
        else
            GetGameFlowController().EnterGameplay(&scene);
    } else if (event.action == "play") {
        GetGameFlowController().EnterGameplay(&scene);
    } else {
        AdjustRuntimeSetting(event.action);
    }
}

void SceneRenderLayer::RefreshRuntimeSettingsLabels() {
    auto percent = [](float value) { return std::to_string(static_cast<int>(std::round(value * 100.0f))) + "%"; };
    AudioEngine& audio = AudioEngine::Get();
    m_RuntimeScreens.SetActionLabel("settings", "masterDown", "Master " + percent(audio.GetMasterVolume()) + "  -");
    m_RuntimeScreens.SetActionLabel("settings", "masterUp", "Master " + percent(audio.GetMasterVolume()) + "  +");
    for (const auto& entry : std::vector<std::pair<AudioBus, std::string>>{
             {AudioBus::Music, "music"}, {AudioBus::Effects, "effects"}, {AudioBus::Voice, "voice"}}) {
        const std::string display =
            entry.second == "music" ? "Music" : (entry.second == "effects" ? "Effects" : "Voice");
        m_RuntimeScreens.SetActionLabel("settings", entry.second + "Down",
                                        display + " " + percent(audio.GetBusVolume(entry.first)) + "  -");
        m_RuntimeScreens.SetActionLabel("settings", entry.second + "Up",
                                        display + " " + percent(audio.GetBusVolume(entry.first)) + "  +");
    }
    const UIAccessibilitySettings& ui = m_UISystem.GetAccessibilitySettings();
    m_RuntimeScreens.SetActionLabel("settings", "uiScaleDown", "UI Scale " + percent(ui.uiScale) + "  -");
    m_RuntimeScreens.SetActionLabel("settings", "uiScaleUp", "UI Scale " + percent(ui.uiScale) + "  +");
    m_RuntimeScreens.SetActionLabel("settings", "subtitles",
                                    std::string("Subtitles: ") + (ui.subtitles ? "On" : "Off"));
    m_RuntimeScreens.SetActionLabel("settings", "highContrast",
                                    std::string("High Contrast: ") + (ui.highContrast ? "On" : "Off"));
}

void SceneRenderLayer::AdjustRuntimeSetting(const std::string& action) {
    const bool increase = action.size() >= 2 && action.rfind("Up") == action.size() - 2;
    const bool decrease = action.size() >= 4 && action.rfind("Down") == action.size() - 4;
    const bool accessibilityAction =
        action == "subtitles" || action == "highContrast" || action.rfind("uiScale", 0) == 0;
    if (!increase && !decrease && !accessibilityAction)
        return;
    const float delta = increase ? 0.1f : -0.1f;
    AudioEngine& audio = AudioEngine::Get();
    if (action.rfind("master", 0) == 0) {
        audio.SetMasterVolume(audio.GetMasterVolume() + delta);
    } else {
        AudioBus bus = AudioBus::Count;
        if (action.rfind("music", 0) == 0)
            bus = AudioBus::Music;
        else if (action.rfind("effects", 0) == 0)
            bus = AudioBus::Effects;
        else if (action.rfind("voice", 0) == 0)
            bus = AudioBus::Voice;
        if (bus != AudioBus::Count)
            audio.SetBusVolume(bus, audio.GetBusVolume(bus) + delta);
    }
    RuntimeUserSettings settings;
    std::string error;
    if (!RuntimeUserSettingsStore::Load(settings, nullptr, &error)) {
        Logger::Warn("[RuntimeUI] Could not load user settings: ", error);
        return;
    }
    settings.audio.master = audio.GetMasterVolume();
    settings.audio.music = audio.GetBusVolume(AudioBus::Music);
    settings.audio.effects = audio.GetBusVolume(AudioBus::Effects);
    settings.audio.voice = audio.GetBusVolume(AudioBus::Voice);
    UIAccessibilitySettings ui = m_UISystem.GetAccessibilitySettings();
    if (action.rfind("uiScale", 0) == 0)
        ui.uiScale = std::clamp(ui.uiScale + delta, 0.5f, 2.0f);
    else if (action == "subtitles")
        ui.subtitles = !ui.subtitles;
    else if (action == "highContrast")
        ui.highContrast = !ui.highContrast;
    if (!m_UISystem.SetAccessibilitySettings(ui, &error)) {
        Logger::Warn("[RuntimeUI] Could not apply accessibility settings: ", error);
        return;
    }
    settings.accessibility.uiScale = ui.uiScale;
    settings.accessibility.subtitleScale = ui.subtitleScale;
    settings.accessibility.subtitles = ui.subtitles;
    settings.accessibility.reduceCameraShake = ui.reduceCameraShake;
    settings.accessibility.highContrast = ui.highContrast;
    settings.accessibility.colorVisionMode = ui.colorVisionMode;
    if (!RuntimeUserSettingsStore::Save(settings, &error))
        Logger::Warn("[RuntimeUI] Could not save audio settings: ", error);
}

void SceneRenderLayer::SyncSceneLoadOverlay() {
    const SceneManager& manager = GetSceneManager();
    UISystemOverlayState overlay;
    if (manager.IsLoading()) {
        overlay.visible = true;
        overlay.progress = manager.GetProgress();
        overlay.title = "Loading";
        overlay.detail = std::string(SceneManager::StageName(manager.GetState())) + " - " + manager.GetRequestedPath();
        overlay.primaryHint = "Please wait";
        overlay.secondaryHint = "Esc: cancel and return to the current scene";
    } else if (manager.GetState() == SceneLoadState::Failed) {
        overlay.visible = true;
        overlay.error = true;
        overlay.progress = 1.0f;
        overlay.title = "Unable to load scene";
        overlay.detail = manager.GetLastError();
        overlay.primaryHint = "R / Enter: retry";
        overlay.secondaryHint = "Esc: return to the current scene";
    }
    m_UISystem.SetSystemOverlay(std::move(overlay));
}

void SceneRenderLayer::OnSceneLoaded() {
    SceneLayer::OnSceneLoaded();
    DefaultSceneFactory::PopulateIfEmpty(GetEditorScene());
}

void SceneRenderLayer::OnRender() {
    m_UISystem.CollectDrawData(GetSimulationScene(), m_UIDrawList);
    if (m_PresentEnabled) {
        m_GameViewport.Render(GetSimulationScene(), true, &m_UIDrawList);
        return;
    }
    if (m_SceneViewportActive) {
        m_Viewport.Render(GetSceneViewportRenderScene(), false);
    }
    if (m_GameViewportActive) {
        m_GameViewport.Render(GetSimulationScene(), false, &m_UIDrawList);
    }
}

void SceneRenderLayer::SetViewportInputEnabled(bool enabled) {
    m_Viewport.SetInputEnabled(enabled);
}

void SceneRenderLayer::SetUIInputViewport(const UIInputViewport& viewport) {
    m_UIInputViewport = viewport;
    const int contextWidth = static_cast<int>(static_cast<float>(viewport.width) * viewport.scaleX);
    const int contextHeight = static_cast<int>(static_cast<float>(viewport.height) * viewport.scaleY);
    m_UISystem.Resize(contextWidth, contextHeight);
}

void SceneRenderLayer::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const {
    m_Viewport.GetViewportRect(outX, outY, outW, outH);
}

bool SceneRenderLayer::BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const {
    return m_Viewport.BuildRayFromScreen(screenX, screenY, outRay);
}
