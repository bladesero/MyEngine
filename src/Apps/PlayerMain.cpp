#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Assets/AssetManager.h"
#include "Audio/AudioEngine.h"
#include "Core/Application.h"
#include "Core/BuildInfo.h"
#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Core/RuntimePerformanceBudget.h"
#include "Core/RuntimeFileSystem.h"
#include "Core/TransactionalFileWriter.h"
#include "Game/SceneRenderLayer.h"
#include "Input/Input.h"
#include "Input/InputGlyphAtlas.h"
#include "Project/ContentArchive.h"
#include "Project/CookManifest.h"
#include "Project/ProjectConfig.h"
#include "Project/RuntimeDependencies.h"
#include "Project/RuntimePerformanceProfile.h"
#include "Project/RuntimeUserSettings.h"
#include "UI/Core/RuntimeUIScreenConfig.h"
#include "Project/SaveGame.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/RenderBackendRegistry.h"
#include "Renderer/RenderPath.h"
#include "Renderer/RHIConformance.h"
#include "Renderer/ShaderCacheService.h"
#include "Renderer/ShaderManager.h"
#include "Miscs/IconsManager.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>

struct PlayerPerformanceStressState {
    uint32_t targetReloads = 0, requestedReloads = 0, completedReloads = 0;
    bool initialReady = false, waiting = false;
    std::string scenePath;
    std::chrono::steady_clock::time_point startupRequestedAt, reloadRequestedAt;
    double initialSceneReadyMs = 0.0, maxSceneReloadMs = 0.0, totalSceneReloadMs = 0.0;
};

class PlayerPerformanceStressLayer final : public Layer {
public:
    PlayerPerformanceStressLayer(SceneRenderLayer& layer, PlayerPerformanceStressState& state)
        : Layer("PlayerPerformanceStress"), m_Layer(layer), m_State(state) {}
    void OnUpdate(float) override {
        const SceneLoadState state = m_Layer.GetSceneManager().GetState();
        if (state != SceneLoadState::Ready)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (!m_State.initialReady) {
            m_State.initialReady = true;
            m_State.initialSceneReadyMs =
                std::chrono::duration<double, std::milli>(now - m_State.startupRequestedAt).count();
        }
        if (m_State.waiting) {
            m_State.waiting = false;
            ++m_State.completedReloads;
            const double elapsed = std::chrono::duration<double, std::milli>(now - m_State.reloadRequestedAt).count();
            m_State.totalSceneReloadMs += elapsed;
            m_State.maxSceneReloadMs = std::max(m_State.maxSceneReloadMs, elapsed);
        }
        if (!m_State.waiting && m_State.requestedReloads < m_State.targetReloads &&
            m_Layer.RequestSceneLoad(m_State.scenePath)) {
            ++m_State.requestedReloads;
            m_State.waiting = true;
            m_State.reloadRequestedAt = std::chrono::steady_clock::now();
        }
    }

private:
    SceneRenderLayer& m_Layer;
    PlayerPerformanceStressState& m_State;
};

static uint64_t ScaleResourceBudget(uint64_t value, double scale) {
    return std::max<uint64_t>(1, static_cast<uint64_t>(static_cast<double>(value) * scale));
}

class PlayerApp : public Application {
public:
    PlayerApp(ApplicationConfig cfg, std::filesystem::path projectRoot, std::string sceneOverride,
              bool runRhiConformance, bool injectDeviceLoss, std::filesystem::path performanceReport,
              std::string performanceProfilePath, RuntimePerformanceBudget performanceBudget, bool warmupOverridden,
              bool minimumOverridden, RuntimeUserSettings userSettings)
        : Application(cfg), m_Backend(cfg.backend), m_VSyncEnabled(cfg.window.vsync),
          m_ProjectRoot(std::move(projectRoot)), m_SceneOverride(std::move(sceneOverride)),
          m_RunRhiConformance(runRhiConformance), m_InjectDeviceLoss(injectDeviceLoss),
          m_PerformanceReportPath(std::move(performanceReport)),
          m_PerformanceProfilePath(std::move(performanceProfilePath)), m_PerformanceCliBudget(performanceBudget),
          m_PerformanceWarmupOverridden(warmupOverridden), m_PerformanceMinimumOverridden(minimumOverridden),
          m_UserSettings(std::move(userSettings)) {}

protected:
    bool OnInit() override {
        IconsManager::Get().ApplyWindowIcon(GetWindow(), IconsManager::kPlayerIcon);
        std::string error;
        const bool allowMissingManifest = !m_SceneOverride.empty();
        if (!m_Project.Open(m_ProjectRoot, allowMissingManifest, &error)) {
            Logger::Error("[Player] ", error);
            return false;
        }
        if (!MountPublishedContent(error)) {
            Logger::Error("[Player] ", error);
            return false;
        }
        if (!InitializePerformanceCapture(error)) {
            Logger::Error("[Performance] ", error);
            return false;
        }
        const std::string requestedScene = m_SceneOverride.empty() ? m_Project.GetStartupScene() : m_SceneOverride;
        std::filesystem::path scenePath;
        if (!m_Project.ResolveScenePath(requestedScene, scenePath, false, &error)) {
            Logger::Error("[Player] ", error);
            return false;
        }
        AssetManager::Get().SetProjectRoot(m_Project.GetRoot());
        ShaderManager::Get().SetShaderCacheMode(m_PublishedContentMounted ? ShaderCacheMode::RuntimeCookedOnly
                                                                          : ShaderCacheMode::EditorOnDemandCompile);
        ShaderCacheService::Get().ClearResolver();
        if (!m_PublishedContentMounted) {
            Logger::Info("[Player] Content.pak not mounted; using development shader compile fallback");
        }
        LoadProjectInputConfig();
        if (RuntimeFileSystem::Get().Exists(InputGlyphAtlas::DefaultPath) &&
            !Input::LoadGlyphAtlasFromFile(InputGlyphAtlas::DefaultPath, &error))
            Logger::Warn("[Player] Input glyph atlas rejected; text binding fallback remains: ", error);
        int localeCount = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&localeCount)) {
            if (localeCount > 0 && locales[0] && locales[0]->language) {
                std::string locale = locales[0]->language;
                if (locales[0]->country && *locales[0]->country)
                    locale += "-" + std::string(locales[0]->country);
                Input::SetGlyphLocale(std::move(locale));
            }
            SDL_free(locales);
        }
        Input::SetRuntimePreferences(m_UserSettings.input.mouseSensitivity, m_UserSettings.input.invertY,
                                     m_UserSettings.input.gamepadDeadZone, m_UserSettings.input.gamepadSensitivity,
                                     m_UserSettings.input.vibration);
        AudioEngine::Get().SetMasterVolume(m_UserSettings.audio.master);
        AudioEngine::Get().SetBusVolume(AudioBus::Music, m_UserSettings.audio.music);
        AudioEngine::Get().SetBusVolume(AudioBus::Effects, m_UserSettings.audio.effects);
        AudioEngine::Get().SetBusVolume(AudioBus::Voice, m_UserSettings.audio.voice);
        if (!m_UserSettings.input.actionMap.is_null() &&
            !Input::ApplyActionMapOverrides(m_UserSettings.input.actionMap, &error)) {
            Logger::Warn("[Player] User input overrides rejected; using project bindings: ", error);
            Input::ResetActionMapOverrides();
        }

        m_RenderContext = CreateRenderContext(m_Backend);
        if (!m_RenderContext) {
            Logger::Error("[Player] Render backend '", RenderBackendToProjectValue(m_Backend),
                          "' is unavailable in this build; available: ", AvailableRenderBackendValues());
            return false;
        }

        if (!m_RenderContext->Init(&GetWindow()))
            return false;
        m_RenderContext->SetVSyncEnabled(m_VSyncEnabled);
        GetEngine().SetFatalHealthCheck([this]() -> std::optional<std::string> {
            CapturePerformanceSample();
            if (!m_RenderContext)
                return std::nullopt;
            RHIDeviceLossInfo info;
            if (m_InjectDeviceLoss && !m_DeviceLossInjected) {
                m_DeviceLossInjected = true;
                info = {RHIDeviceLossReason::Removed, -1, 1, "release-gate synthetic device removal"};
            } else {
                if (!m_RenderContext->IsDeviceLost())
                    return std::nullopt;
                info = m_RenderContext->GetDeviceLossInfo();
            }
            std::ostringstream message;
            message << "RHI device lost backend=" << RenderBackendToProjectValue(m_Backend)
                    << " reason=" << RHIDeviceLossReasonName(info.reason) << " nativeCode=" << info.nativeCode
                    << " generation=" << info.deviceGeneration << " diagnostic=" << info.diagnostic;
            return message.str();
        });
        if (m_RunRhiConformance) {
            const RHIConformanceReport report = RunRHIConformance(*m_RenderContext);
            if (!report.passed) {
                Logger::Error("[RHIConformance] ", report.Summary());
                return false;
            }
            Logger::Info("[RHIConformance] ", report.Summary());
        }

        auto& window = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(m_RenderContext.get(), window.GetWidth(), window.GetHeight());
        m_SceneLayer = sceneLayer;
        sceneLayer->SetPresentEnabled(true);
        sceneLayer->SetRuntimeScreensEnabled(true);
        if (RuntimeFileSystem::Get().Exists(RuntimeUIScreenConfig::DefaultPath) &&
            !sceneLayer->LoadRuntimeScreenConfig(RuntimeUIScreenConfig::DefaultPath, &error))
            Logger::Warn("[Player] Project runtime screens rejected; using standard fallback: ", error);
        sceneLayer->SetPauseWhenUnfocused(m_UserSettings.display.pauseWhenUnfocused);
        RuntimeResourceBudgetConfig resourceBudget;
        resourceBudget.assetCpuHighWatermarkBytes = static_cast<size_t>(
            ScaleResourceBudget(resourceBudget.assetCpuHighWatermarkBytes, m_PerformanceResourceBudgetScale));
        resourceBudget.maxLiveActors =
            ScaleResourceBudget(resourceBudget.maxLiveActors, m_PerformanceResourceBudgetScale);
        resourceBudget.maxPendingUploadBytes =
            ScaleResourceBudget(resourceBudget.maxPendingUploadBytes, m_PerformanceResourceBudgetScale);
        resourceBudget.maxPendingUploadTasks = static_cast<size_t>(
            ScaleResourceBudget(resourceBudget.maxPendingUploadTasks, m_PerformanceResourceBudgetScale));
        resourceBudget.maxGpuResourceBytes =
            ScaleResourceBudget(resourceBudget.maxGpuResourceBytes, m_PerformanceResourceBudgetScale);
        resourceBudget.maxLogicalDescriptors =
            ScaleResourceBudget(resourceBudget.maxLogicalDescriptors, m_PerformanceResourceBudgetScale);
        resourceBudget.maxNativeDescriptorSlots =
            ScaleResourceBudget(resourceBudget.maxNativeDescriptorSlots, m_PerformanceResourceBudgetScale);
        if (!sceneLayer->EnableRuntimeResourceBudget(resourceBudget, &error)) {
            Logger::Error("[Player] Invalid runtime resource budget: ", error);
            return false;
        }
        UIAccessibilitySettings uiAccessibility;
        uiAccessibility.uiScale = m_UserSettings.accessibility.uiScale;
        uiAccessibility.subtitleScale = m_UserSettings.accessibility.subtitleScale;
        uiAccessibility.subtitles = m_UserSettings.accessibility.subtitles;
        uiAccessibility.reduceCameraShake = m_UserSettings.accessibility.reduceCameraShake;
        uiAccessibility.highContrast = m_UserSettings.accessibility.highContrast;
        uiAccessibility.colorVisionMode = m_UserSettings.accessibility.colorVisionMode;
        if (!sceneLayer->SetUIAccessibilitySettings(uiAccessibility, &error)) {
            Logger::Warn("[Player] Accessibility settings rejected: ", error);
        }
        sceneLayer->SetRenderPath(m_Project.GetGraphicsSettings().renderPath == "deferred" ? RenderPath::Deferred
                                                                                           : RenderPath::Forward);
        GetEngine().PushLayer(sceneLayer);
        if (!sceneLayer->BeginPlay()) {
            Logger::Error("[Player] Failed to enter Play mode");
            return false;
        }
        m_StressState.startupRequestedAt = std::chrono::steady_clock::now();
        if (!sceneLayer->RequestSceneLoad(requestedScene)) {
            Logger::Error("[Player] Startup scene request was rejected: ", requestedScene);
            return false;
        }
        m_ResolvedScenePath = requestedScene;
        m_StressState.targetReloads = m_PerformanceSceneReloadCount;
        m_StressState.scenePath = requestedScene;
        if (m_PerformanceGate)
            GetEngine().PushLayer(new PlayerPerformanceStressLayer(*sceneLayer, m_StressState));
        Logger::Info("[Player] Loading startup scene: ", requestedScene);
        return true;
    }

    void OnBeforeLayersCleared() override { WritePerformanceReport(); }

    void OnShutdown() override {
        SaveGame::ClearStorageRootOverride();
        RuntimeUserSettingsStore::ClearStorageRootOverride();
        if (m_RenderContext) {
            m_RenderContext->Shutdown();
            m_RenderContext.reset();
        }
        AssetManager::Get().SetProjectRoot({});
        AssetManager::Get().SetEngineContentRoot({});
        RuntimeFileSystem::Set({});
    }

private:
    bool InitializePerformanceCapture(std::string& error) {
        if (m_PerformanceReportPath.empty())
            return true;
        RuntimePerformanceProfile profile;
        RuntimePerformanceBudget budget = m_PerformanceCliBudget;
        const bool explicitProfile = !m_PerformanceProfilePath.empty();
        const std::string profilePath =
            explicitProfile ? m_PerformanceProfilePath : "Content/Config/Performance.profile.json";
        if (RuntimeFileSystem::Get().Exists(profilePath)) {
            std::string text;
            if (!RuntimeFileSystem::Get().ReadText(profilePath, text, &error) ||
                !RuntimePerformanceProfile::FromText(text, profile, &error)) {
                error = "failed to load performance profile '" + profilePath + "': " + error;
                return false;
            }
            budget = profile.budget;
            m_PerformanceProfileName = profile.name;
            m_PerformanceHardwareClass = profile.hardwareClass;
            m_PerformanceResolvedProfilePath = profilePath;
            m_PerformanceScenario = profile.scenario;
            m_PerformanceResourceBudgetScale = profile.resourceBudgetScale;
            m_PerformanceSceneReloadCount = profile.sceneReloadCount;
            m_MaxInitialSceneReadyMs = profile.maxInitialSceneReadyMs;
            m_MaxSceneReloadMs = profile.maxSceneReloadMs;
        } else if (explicitProfile) {
            error = "performance profile does not exist: " + profilePath;
            return false;
        } else {
            m_PerformanceProfileName = "builtin-default";
            m_PerformanceHardwareClass = "unspecified";
            m_PerformanceResolvedProfilePath = "builtin";
        }
        if (m_PerformanceWarmupOverridden)
            budget.warmupSamples = m_PerformanceCliBudget.warmupSamples;
        if (m_PerformanceMinimumOverridden)
            budget.minimumSamples = m_PerformanceCliBudget.minimumSamples;
        m_PerformanceGate = std::make_unique<RuntimePerformanceGate>(budget);
        return true;
    }

    void CapturePerformanceSample() {
        if (!m_PerformanceGate)
            return;
        const FrameStats& frame = GetEngine().GetFrameStats();
        const RendererFrameStats& renderer = frame.renderer;
        const double gpuMs = renderer.gpuTimingAvailable
                                 ? static_cast<double>(renderer.shadowGpuMs + renderer.mainGpuMs + renderer.ssaoGpuMs +
                                                       renderer.compositeGpuMs)
                                 : 0.0;
        uint32_t droppedTicks = 0;
        if (m_SceneLayer) {
            droppedTicks = m_SceneLayer->GetSimulationScene().GetFrameScheduler().GetStats().droppedFixedTicks;
        }
        m_PerformanceGate->AddSample({frame.frameMs, frame.updateMs, frame.renderMs, gpuMs,
                                      GetCurrentProcessWorkingSetBytes(), droppedTicks, renderer.gpuTimingAvailable});
    }

    void WritePerformanceReport() {
        if (!m_PerformanceGate)
            return;
        const RuntimePerformanceReport report = m_PerformanceGate->Evaluate();
        nlohmann::json value = nlohmann::json::parse(report.ToJson());
        value["schemaVersion"] = 1;
        value["provenance"] = {{"engineVersion", std::string(BuildInfo::EngineVersion)},
                               {"buildId", std::string(BuildInfo::BuildId)},
                               {"gitCommit", std::string(BuildInfo::GitCommit)},
                               {"configuration", std::string(BuildInfo::Configuration)},
                               {"compiler", std::string(BuildInfo::Compiler)},
                               {"shaderTool", std::string(BuildInfo::ShaderTool)}};
        value["capture"] = {{"backend", RenderBackendToProjectValue(m_Backend)},
                            {"scene", m_ResolvedScenePath},
                            {"width", GetWindow().GetWidth()},
                            {"height", GetWindow().GetHeight()},
                            {"capturedUnixMilliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                             std::chrono::system_clock::now().time_since_epoch())
                                                             .count()}};
        const RHIDeviceIdentity identity = m_RenderContext ? m_RenderContext->GetDeviceIdentity() : RHIDeviceIdentity{};
        value["device"] = {{"adapterName", identity.adapterName},
                           {"driverVersion", identity.driverVersion},
                           {"vendorId", identity.vendorId},
                           {"deviceId", identity.deviceId},
                           {"subsystemId", identity.subsystemId},
                           {"revision", identity.revision},
                           {"dedicatedVideoMemoryBytes", identity.dedicatedVideoMemoryBytes},
                           {"softwareAdapter", identity.softwareAdapter}};
        value["profile"] = {{"name", m_PerformanceProfileName},
                            {"hardwareClass", m_PerformanceHardwareClass},
                            {"source", m_PerformanceResolvedProfilePath},
                            {"scenario", m_PerformanceScenario},
                            {"resourceBudgetScale", m_PerformanceResourceBudgetScale},
                            {"sceneReloadCount", m_PerformanceSceneReloadCount},
                            {"maxInitialSceneReadyMs", m_MaxInitialSceneReadyMs},
                            {"maxSceneReloadMs", m_MaxSceneReloadMs},
                            {"warmupSamples", m_PerformanceGate->GetBudget().warmupSamples},
                            {"minimumSamples", m_PerformanceGate->GetBudget().minimumSamples}};
        const RuntimePerformanceBudget& appliedBudget = m_PerformanceGate->GetBudget();
        value["profile"]["budget"] = {{"maxP95FrameMs", appliedBudget.maxP95FrameMs},
                                      {"maxP99FrameMs", appliedBudget.maxP99FrameMs},
                                      {"maxFrameMs", appliedBudget.maxFrameMs},
                                      {"maxP95GpuMs", appliedBudget.maxP95GpuMs},
                                      {"maxWorkingSetGrowthBytes", appliedBudget.maxWorkingSetGrowthBytes},
                                      {"maxDroppedFixedTicks", appliedBudget.maxDroppedFixedTicks}};
        if (m_SceneLayer) {
            const auto& resources = m_SceneLayer->GetRuntimeResourceBudgetReport();
            value["resources"] = {
                {"assetCpuBytes", resources.assets.bytesAfter},
                {"assetEvictedBytes", resources.assets.bytesBefore - resources.assets.bytesAfter},
                {"assetBlockedBytes", resources.assets.blockedBytes},
                {"pendingUploadTasks", resources.uploads.pendingTasks},
                {"pendingUploadBytes", resources.uploads.pendingBytes},
                {"peakPendingUploadBytes", resources.uploads.peakPendingBytes},
                {"gpuResourceBytes", resources.rhi.liveResourceBytes},
                {"peakGpuResourceBytes", resources.rhi.peakResourceBytes},
                {"liveGpuDescriptors", resources.rhi.liveDescriptors},
                {"liveNativeDescriptorSlots", resources.rhi.liveNativeDescriptorSlots},
                {"peakNativeDescriptorSlots", resources.rhi.peakNativeDescriptorSlots},
                {"failedNativeDescriptorAllocations", resources.rhi.failedNativeDescriptorAllocations},
                {"gpuTextureEvictedBytes", resources.gpuTextures.bytesBefore - resources.gpuTextures.bytesAfter},
                {"gpuTextureBlockedBytes", resources.gpuTextures.blockedBytes},
                {"gpuTextureEvictions", resources.gpuTextures.evictions.size()},
                {"gpuMeshEvictedBytes", resources.gpuMeshes.bytesBefore - resources.gpuMeshes.bytesAfter},
                {"gpuMeshBlockedBytes", resources.gpuMeshes.blockedBytes},
                {"gpuMeshEvictions", resources.gpuMeshes.evictions.size()},
                {"qualityDegradationLevel", resources.qualityDegradationLevel},
                {"qualityDegradationTransitions", resources.qualityDegradationTransitions},
                {"liveActors", resources.liveActors},
                {"pressureFrames", resources.pressureFrames},
                {"violationTransitions", resources.violationTransitions},
                {"violations", resources.violations}};
        }
        value["stress"] = {
            {"targetReloads", m_StressState.targetReloads},
            {"requestedReloads", m_StressState.requestedReloads},
            {"completedReloads", m_StressState.completedReloads},
            {"waiting", m_StressState.waiting},
            {"initialReady", m_StressState.initialReady},
            {"initialSceneReadyMs", m_StressState.initialSceneReadyMs},
            {"maxSceneReloadMs", m_StressState.maxSceneReloadMs},
            {"averageSceneReloadMs",
             m_StressState.completedReloads ? m_StressState.totalSceneReloadMs / m_StressState.completedReloads : 0.0}};
        if (!m_StressState.initialReady) {
            value["passed"] = false;
            value["violations"].push_back("initial scene did not become ready");
        } else if (m_StressState.initialSceneReadyMs > m_MaxInitialSceneReadyMs) {
            value["passed"] = false;
            value["violations"].push_back("initial scene ready budget exceeded");
        }
        if (m_StressState.completedReloads < m_StressState.targetReloads) {
            value["passed"] = false;
            value["violations"].push_back("scene reload stress did not complete");
        }
        if (m_StressState.completedReloads && m_StressState.maxSceneReloadMs > m_MaxSceneReloadMs) {
            value["passed"] = false;
            value["violations"].push_back("scene reload latency budget exceeded");
        }
        TransactionalWriteOptions options;
        options.validator = [](const std::filesystem::path& path, std::string* error) {
            try {
                std::ifstream input(path);
                nlohmann::json parsed;
                input >> parsed;
                if (!parsed.is_object() || parsed.value("schemaVersion", 0) != 1) {
                    if (error)
                        *error = "invalid runtime performance report schema";
                    return false;
                }
                return true;
            } catch (const std::exception& exception) {
                if (error)
                    *error = exception.what();
                return false;
            }
        };
        std::string error;
        if (!TransactionalFileWriter::WriteText(m_PerformanceReportPath, value.dump(2) + "\n", options, &error)) {
            Logger::Error("[Performance] Failed to write report: ", error);
            if (GetEngine().GetExitCode() == 0)
                GetEngine().SetExitCode(4);
            return;
        }
        Logger::Info("[Performance] report=", m_PerformanceReportPath.string(),
                     " passed=", value.value("passed", false), " samples=", report.summary.sampleCount,
                     " p95FrameMs=", report.summary.p95FrameMs);
        if (!value.value("passed", false) && GetEngine().GetExitCode() == 0)
            GetEngine().SetExitCode(4);
    }

    bool MountPublishedContent(std::string& error) {
        const std::filesystem::path archive = m_Project.GetRoot() / ContentArchive::kFileName;
        m_PublishedContentMounted = false;
        if (!std::filesystem::is_regular_file(archive)) {
            RuntimeFileSystem::Set({});
            return true;
        }

        CookManifest manifest;
        if (!CookManifest::Load(m_Project.GetRoot() / CookManifest::kFileName, manifest, &error)) {
            return false;
        }
        const std::string archiveHash = ContentArchive::HashFile(archive, &error);
        if (archiveHash.empty())
            return false;
        if (archiveHash != manifest.archiveHash) {
            error = "Content archive hash does not match Cook manifest";
            return false;
        }

        auto reader = std::make_shared<ContentArchiveReader>();
        if (!reader->Open(archive, &error) || !reader->ValidateAgainstManifest(manifest, &error)) {
            return false;
        }
        auto mounted = std::make_shared<MountedFileSystem>();
        mounted->SetProjectRoot(m_Project.GetRoot());
        mounted->AddMount(std::make_shared<PakFileSystem>(reader));
        mounted->AddMount(std::make_shared<PackageRootFileSystem>(m_Project.GetRoot()));
        RuntimeFileSystem::Set(mounted);
        m_PublishedContentMounted = true;
        Logger::Info("[Player] Mounted Content.pak: ", archive.string(), " entries=", reader->EntryCount(),
                     " contentBytes=", reader->ContentBytes(), " buildId=", manifest.buildId,
                     " configuration=", manifest.configuration, " shaderPolicy=RuntimeCookedOnly");
        return true;
    }

    void LoadProjectInputConfig() {
        std::string error;
        std::filesystem::path inputConfig;
        if (!m_Project.ResolveInputConfigPath(inputConfig, false, &error)) {
            Logger::Warn("[Player] Input config path invalid: ", error, "; using default input map");
            Input::SetDefaultActionMap();
            return;
        }
        if (!RuntimeFileSystem::Get().Exists(inputConfig.string())) {
            Logger::Warn("[Player] Input config not found: ", inputConfig.string(), "; using default input map");
            Input::SetDefaultActionMap();
            return;
        }
        if (!Input::LoadActionMapFromFile(inputConfig, &error)) {
            Logger::Warn("[Player] Failed to load input config: ", error, "; using default input map");
            return;
        }
        Logger::Info("[Player] Loaded input config: ", inputConfig.string());
    }

    std::unique_ptr<IRenderContext> m_RenderContext;
    RenderBackend m_Backend = kDefaultRenderBackend;
    bool m_VSyncEnabled = true;
    bool m_PublishedContentMounted = false;
    std::filesystem::path m_ProjectRoot;
    std::string m_SceneOverride;
    ProjectConfig m_Project;
    bool m_RunRhiConformance = false;
    bool m_InjectDeviceLoss = false;
    bool m_DeviceLossInjected = false;
    SceneRenderLayer* m_SceneLayer = nullptr;
    std::filesystem::path m_ResolvedScenePath;
    std::filesystem::path m_PerformanceReportPath;
    std::string m_PerformanceProfilePath;
    RuntimePerformanceBudget m_PerformanceCliBudget;
    bool m_PerformanceWarmupOverridden = false;
    bool m_PerformanceMinimumOverridden = false;
    RuntimeUserSettings m_UserSettings;
    std::string m_PerformanceProfileName;
    std::string m_PerformanceHardwareClass;
    std::string m_PerformanceResolvedProfilePath;
    std::string m_PerformanceScenario = "warm-gameplay";
    double m_PerformanceResourceBudgetScale = 1.0;
    uint32_t m_PerformanceSceneReloadCount = 0;
    double m_MaxInitialSceneReadyMs = 10000.0;
    double m_MaxSceneReloadMs = 5000.0;
    PlayerPerformanceStressState m_StressState;
    std::unique_ptr<RuntimePerformanceGate> m_PerformanceGate;
};

static bool ParseBackend(const std::string& value, ApplicationConfig& cfg) {
    const auto backend = ParseRenderBackend(value);
    if (!backend) {
        Logger::Error("Unknown backend: ", value, " (use ", AvailableRenderBackendValues(), ")");
        return false;
    }
    if (!IsBackendCompiled(*backend)) {
        Logger::Error("Backend '", RenderBackendToProjectValue(*backend),
                      "' is not compiled in; available: ", AvailableRenderBackendValues());
        return false;
    }
    cfg.backend = *backend;
    return true;
}

static bool ParseVSync(const std::string& value, ApplicationConfig& cfg) {
    if (value == "on" || value == "true" || value == "1") {
        cfg.window.vsync = true;
        return true;
    }
    if (value == "off" || value == "false" || value == "0") {
        cfg.window.vsync = false;
        return true;
    }
    Logger::Error("Unknown vsync value: ", value, " (use on/off)");
    return false;
}

static std::filesystem::path ResolveExecutableDirectory() {
    if (const char* basePath = SDL_GetBasePath()) {
        return std::filesystem::absolute(std::filesystem::path(basePath)).lexically_normal();
    }
    std::error_code ec;
    return std::filesystem::absolute(std::filesystem::current_path(), ec).lexically_normal();
}

static void ApplyProjectBackend(const std::filesystem::path& projectRoot, ApplicationConfig& cfg) {
    ProjectConfig project;
    std::string error;
    if (!project.Open(projectRoot, false, &error)) {
        Logger::Warn("[Player] Failed to read project graphics settings: ", error);
        return;
    }
    if (!ParseBackend(project.GetGraphicsSettings().backend, cfg)) {
        cfg.backend = kDefaultRenderBackend;
        Logger::Warn("[Player] Project graphics backend '", project.GetGraphicsSettings().backend,
                     "' is unavailable; falling back to ", RenderBackendToProjectValue(kDefaultRenderBackend));
    }
}

static void ConfigurePlayerUserData(const std::filesystem::path& projectRoot) {
    ProjectConfig project;
    std::string ignored;
    project.Open(projectRoot, true, &ignored);
    const std::string userDataId = project.GetProjectId().empty()
                                       ? (project.GetName().empty() ? std::string("MyEngineGame") : project.GetName())
                                       : project.GetProjectId();
    std::filesystem::path userRoot;
    if (char* preferencePath = SDL_GetPrefPath("MyEngine", userDataId.c_str())) {
        userRoot = preferencePath;
        SDL_free(preferencePath);
    } else {
        userRoot = projectRoot / "Saved";
        Logger::Warn("[Player] SDL_GetPrefPath failed; user data uses project fallback: ", SDL_GetError());
    }
    SaveGame::SetStorageRoot(userRoot / "SaveGames");
    RuntimeUserSettingsStore::SetStorageRoot(userRoot / "Settings");
}

static WindowMode ParseWindowMode(const std::string& value) {
    if (value == "borderless")
        return WindowMode::Borderless;
    if (value == "fullscreen")
        return WindowMode::Fullscreen;
    return WindowMode::Windowed;
}

static RuntimeUserSettings ApplyPlayerUserSettings(ApplicationConfig& cfg, bool backendOverridden,
                                                   bool vsyncOverridden) {
    const bool settingsExist = std::filesystem::is_regular_file(RuntimeUserSettingsStore::GetSettingsPath());
    RuntimeUserSettings settings = RuntimeUserSettingsStore::Defaults();
    bool usedBackup = false;
    std::string error;
    if (settingsExist && !RuntimeUserSettingsStore::Load(settings, &usedBackup, &error)) {
        Logger::Warn("[Player] User settings rejected; using project/default values: ", error);
        return settings;
    }
    if (!settingsExist) {
        settings.display.width = cfg.window.width;
        settings.display.height = cfg.window.height;
        settings.display.vsync = cfg.window.vsync;
        settings.display.frameRateLimit = cfg.engine.targetFps;
        settings.graphics.backend = RenderBackendToProjectValue(cfg.backend);
        if (!RuntimeUserSettingsStore::Save(settings, &error))
            Logger::Warn("[Player] Failed to create default user settings: ", error);
    }
    if (usedBackup)
        Logger::Warn("[Player] Loaded last-known-good user settings backup");
    cfg.window.width = settings.display.width;
    cfg.window.height = settings.display.height;
    cfg.window.mode = ParseWindowMode(settings.display.windowMode);
    cfg.engine.targetFps = settings.display.frameRateLimit;
    if (!vsyncOverridden)
        cfg.window.vsync = settings.display.vsync;
    if (!backendOverridden && !ParseBackend(settings.graphics.backend, cfg))
        Logger::Warn("[Player] User backend unavailable; retaining project backend");
    return settings;
}

static int RunPlayer(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title = "MyEngine Player";
    cfg.window.width = 1280;
    cfg.window.height = 720;
    cfg.window.vsync = true;
    cfg.engine.appName = "MyEnginePlayer";
    cfg.engine.targetFps = 60;

    const std::filesystem::path executableDirectory = ResolveExecutableDirectory();
    std::filesystem::path projectRoot = std::filesystem::is_regular_file(executableDirectory / ProjectConfig::kFileName)
                                            ? executableDirectory
                                            : std::filesystem::current_path();
    std::string sceneOverride;
    std::filesystem::path performanceReport;
    std::string performanceProfilePath;
    RuntimePerformanceBudget performanceBudget;
    performanceBudget.warmupSamples = 60;
    bool performanceWarmupOverridden = false;
    bool performanceMinimumOverridden = false;
    bool backendOverridden = false;
    bool vsyncOverridden = false;
    bool runRhiConformance = false;
    bool injectDeviceLoss = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" || arg == "--scene" || arg == "--backend" || arg == "--vsync" ||
            arg == "--auto-quit-seconds" || arg == "--performance-report" || arg == "--performance-profile" ||
            arg == "--performance-warmup-frames" || arg == "--performance-min-samples") {
            if (i + 1 >= argc) {
                Logger::Error("Missing value for ", arg);
                return 2;
            }
            const std::string value = argv[++i];
            if (arg == "--project")
                projectRoot = value;
            else if (arg == "--scene")
                sceneOverride = value;
            else if (arg == "--performance-report")
                performanceReport = value;
            else if (arg == "--performance-profile")
                performanceProfilePath = value;
            else if (arg == "--performance-warmup-frames" || arg == "--performance-min-samples") {
                size_t parsed = 0;
                try {
                    parsed = static_cast<size_t>(std::stoull(value));
                } catch (...) {
                    Logger::Error("Invalid performance sample count: ", value);
                    return 2;
                }
                if (parsed == 0 || parsed > 1000000)
                    return 2;
                if (arg == "--performance-warmup-frames") {
                    performanceBudget.warmupSamples = parsed;
                    performanceWarmupOverridden = true;
                } else {
                    performanceBudget.minimumSamples = parsed;
                    performanceMinimumOverridden = true;
                }
            } else if (arg == "--backend") {
                if (!ParseBackend(value, cfg))
                    return 2;
                backendOverridden = true;
            } else if (arg == "--auto-quit-seconds") {
                try {
                    cfg.engine.autoQuitAfterSeconds = std::stof(value);
                } catch (...) {
                    Logger::Error("Invalid auto quit duration: ", value);
                    return 2;
                }
                if (cfg.engine.autoQuitAfterSeconds <= 0.0f)
                    return 2;
            } else {
                if (!ParseVSync(value, cfg))
                    return 2;
                vsyncOverridden = true;
            }
        } else if (arg.rfind("--project=", 0) == 0) {
            projectRoot = arg.substr(std::string("--project=").size());
        } else if (arg.rfind("--scene=", 0) == 0) {
            sceneOverride = arg.substr(std::string("--scene=").size());
        } else if (arg.rfind("--backend=", 0) == 0) {
            if (!ParseBackend(arg.substr(std::string("--backend=").size()), cfg))
                return 2;
            backendOverridden = true;
        } else if (arg.rfind("--vsync=", 0) == 0) {
            if (!ParseVSync(arg.substr(std::string("--vsync=").size()), cfg))
                return 2;
            vsyncOverridden = true;
        } else if (arg.rfind("--auto-quit-seconds=", 0) == 0) {
            try {
                cfg.engine.autoQuitAfterSeconds = std::stof(arg.substr(std::string("--auto-quit-seconds=").size()));
            } catch (...) {
                Logger::Error("Invalid auto quit duration");
                return 2;
            }
            if (cfg.engine.autoQuitAfterSeconds <= 0.0f)
                return 2;
        } else if (arg.rfind("--performance-report=", 0) == 0) {
            performanceReport = arg.substr(std::string("--performance-report=").size());
        } else if (arg.rfind("--performance-profile=", 0) == 0) {
            performanceProfilePath = arg.substr(std::string("--performance-profile=").size());
        } else if (arg == "--rhi-conformance") {
            runRhiConformance = true;
        } else if (arg == "--rhi-test-inject-device-loss") {
            injectDeviceLoss = true;
        } else {
            Logger::Error("Unknown argument: ", arg);
            return 2;
        }
    }

    if (injectDeviceLoss && !runRhiConformance) {
        Logger::Error("--rhi-test-inject-device-loss requires --rhi-conformance");
        return 2;
    }
    if (!performanceReport.empty() && performanceReport.filename().empty()) {
        Logger::Error("--performance-report must name a JSON file");
        return 2;
    }
    if (!performanceProfilePath.empty() && performanceReport.empty()) {
        Logger::Error("--performance-profile requires --performance-report");
        return 2;
    }

    std::string packageError;
    if (!RuntimeDependencyManifest::ValidatePackage(executableDirectory, &packageError)) {
        Logger::Error("[Player] ", packageError);
        return 1;
    }

    if (!backendOverridden) {
        ApplyProjectBackend(projectRoot, cfg);
    }
    ConfigurePlayerUserData(projectRoot);
    RuntimeUserSettings userSettings = ApplyPlayerUserSettings(cfg, backendOverridden, vsyncOverridden);

    PlayerApp app(cfg, std::move(projectRoot), std::move(sceneOverride), runRhiConformance, injectDeviceLoss,
                  std::move(performanceReport), std::move(performanceProfilePath), performanceBudget,
                  performanceWarmupOverridden, performanceMinimumOverridden, std::move(userSettings));
    return app.Run();
}

int main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, RunPlayer, nullptr);
}
