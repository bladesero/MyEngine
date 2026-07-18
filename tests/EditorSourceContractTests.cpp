#include "TestHarness.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool TestEditorUIFacadeDoesNotExposeRawImGuiContract() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorUI/EditorUIFacade.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path())
                break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };

    const std::filesystem::path root = findRepositoryRoot();
    std::ifstream source(root / "src/Editor/EditorUI/EditorUIFacade.cpp", std::ios::binary);
    if (!Check(static_cast<bool>(source), "failed to open EditorUIFacade source"))
        return false;
    std::stringstream buffer;
    buffer << source.rdbuf();
    const std::string text = buffer.str();
    if (!Check(text.find("SetDefaultNamespace(\"UI\")") != std::string::npos,
               "UI facade does not register the UI namespace"))
        return false;
    if (!Check(text.find("SetDefaultNamespace(\"ImGui\")") == std::string::npos,
               "UI facade exposes raw ImGui namespace to scripts"))
        return false;
    if (!Check(text.find("ImGui::") != std::string::npos,
               "UI facade source contract test did not inspect ImGui wrapper usage"))
        return false;
    if (!Check(text.find("SetDefaultNamespace(\"PanelState\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"Hierarchy\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"AssetBrowser\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"Validation\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"Project\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"DragDrop\")") != std::string::npos &&
                   text.find("SetDefaultNamespace(\"Transaction\")") != std::string::npos,
               "UI facade does not expose the editor binding namespaces"))
        return false;
    if (!Check(text.find("ExecuteCommand(") == std::string::npos &&
                   text.find("WatchForChanges(") == std::string::npos &&
                   text.find("GetSelection().Select") == std::string::npos,
               "AS facade mutating bindings bypass editor operators"))
        return false;
    return true;
}

bool TestEditorLayerKeepsCppPanelsWithScriptSidecar() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorLayer.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path())
                break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };

    const std::filesystem::path root = findRepositoryRoot();
    std::ifstream source(root / "src/Editor/EditorLayer.cpp", std::ios::binary);
    if (!Check(static_cast<bool>(source), "failed to open EditorLayer source"))
        return false;
    std::stringstream buffer;
    buffer << source.rdbuf();
    const std::string text = buffer.str();
    if (!Check(text.find("std::make_unique<ToolbarPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native toolbar panel"))
        return false;
    if (!Check(text.find("std::make_unique<SceneViewportPanel>(gizmo)") != std::string::npos,
               "EditorLayer no longer creates the native scene viewport panel"))
        return false;
    if (!Check(text.find("std::make_unique<GameViewportPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native game viewport panel"))
        return false;
    if (!Check(text.find("std::make_unique<SceneHierarchyPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native hierarchy panel"))
        return false;
    if (!Check(text.find("std::make_unique<InspectorPanel>(gizmo)") != std::string::npos,
               "EditorLayer no longer creates the native inspector panel"))
        return false;
    if (!Check(text.find("std::make_unique<AssetBrowserPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native asset browser panel"))
        return false;
    if (!Check(text.find("ScriptedEditorPanel") == std::string::npos,
               "EditorLayer must not replace native panels with scripted dock panels"))
        return false;
    if (!Check(text.find("std::make_unique<ScriptedToolPanel>") != std::string::npos,
               "EditorLayer does not append scripted tool panels for extensions"))
        return false;
    if (!Check(text.find("Sidecar domain") != std::string::npos,
               "EditorLayer script domain is not documented as sidecar-only"))
        return false;
    return true;
}

bool TestEditorPlatformViewportDpiContract() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "xmake/packages.lua")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path())
                break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };
    const auto readSource = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    };

    const std::filesystem::path root = findRepositoryRoot();
    if (!Check(!root.empty(), "failed to locate repository root for viewport DPI contract"))
        return false;

    const std::string packages = readSource(root / "xmake/packages.lua");
    const std::string imnodesPackage = readSource(root / "packages/i/imnodes/xmake.lua");
    const std::string imnodesPatch = readSource(root / "packages/i/imnodes/patches/v0.5-imgui-1.92.patch");
    const std::string window = readSource(root / "src/Runtime/Core/Window.cpp");
    const std::string windowHeader = readSource(root / "src/Runtime/Core/Window.h");
    const std::string eventHeader = readSource(root / "src/Runtime/Core/Event.h");
    const std::string engine = readSource(root / "src/Runtime/Core/Engine.cpp");
    const std::string sceneRenderLayer = readSource(root / "src/Runtime/Game/SceneRenderLayer.cpp");
    const std::string d3d11 = readSource(root / "src/Runtime/Renderer/D3D11Context.cpp");
    const std::string d3d12 = readSource(root / "src/Runtime/Renderer/D3D12Context.cpp");
    const std::string vulkan = readSource(root / "src/Runtime/Renderer/VulkanContext.cpp");
    const std::string metal = readSource(root / "src/Runtime/Renderer/MetalContext.mm");
    const std::string manifest = readSource(root / "src/Runtime/Miscs/Resources/MyEngineEditor.manifest");
    const std::string resource = readSource(root / "src/Runtime/Miscs/Resources/MyEngineEditor.rc");
    const std::string backend = readSource(root / "src/Editor/EditorImGuiBackend.cpp");
    const std::string graphPanel = readSource(root / "src/Editor/Panels/ShaderGraphPanel.cpp");

    if (!Check(packages.find("v1.92.7-docking") != std::string::npos &&
                   packages.find("v1.91.3-docking") == std::string::npos,
               "Editor and imnodes are not pinned to the ImGui 1.92.7 docking ABI"))
        return false;
    if (!Check(packages.find("myengine_imgui_192 = true") != std::string::npos &&
                   imnodesPackage.find("add_configs(\"myengine_imgui_192\"") != std::string::npos &&
                   imnodesPackage.find("add_deps(\"imgui v1.92.7-docking\")") != std::string::npos &&
                   imnodesPatch.find("IMGUI_VERSION_NUM != 19270") != std::string::npos,
               "imnodes package can reuse a stale binary built against an incompatible ImGui ABI"))
        return false;
    if (!Check(window.find("SDL_WINDOW_HIGH_PIXEL_DENSITY") != std::string::npos &&
                   manifest.find("PerMonitorV2") != std::string::npos &&
                   resource.find("MyEngineEditor.manifest") != std::string::npos,
               "Windows Editor high-DPI manifest/window contract is incomplete"))
        return false;
    if (!Check(window.find("SDL_GetWindowSize(m_Window") != std::string::npos &&
                   window.find("SDL_GetWindowSizeInPixels(m_Window") != std::string::npos &&
                   windowHeader.find("GetPixelWidth() const") != std::string::npos &&
                   windowHeader.find("GetPixelHeight() const") != std::string::npos &&
                   eventHeader.find("int pixelWidth = 0") != std::string::npos &&
                   eventHeader.find("int pixelHeight = 0") != std::string::npos &&
                   engine.find("SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED") != std::string::npos &&
                   engine.find("e.resize.pixelWidth = m_Window->GetPixelWidth()") != std::string::npos &&
                   engine.find("e.resize.pixelHeight = m_Window->GetPixelHeight()") != std::string::npos &&
                   sceneRenderLayer.find("swapChain->Resize(targetWidth, targetHeight)") != std::string::npos &&
                   d3d11.find("window->GetPixelWidth()") != std::string::npos &&
                   d3d12.find("window->GetPixelWidth()") != std::string::npos &&
                   vulkan.find("window->GetPixelWidth()") != std::string::npos &&
                   metal.find("window->GetPixelWidth()") != std::string::npos,
               "window resize mixes logical dock coordinates with drawable swapchain pixels"))
        return false;
    if (!Check(backend.find("ImGui_ImplDX12_InitInfo") != std::string::npos &&
                   backend.find("initInfo.CommandQueue = commandQueue") != std::string::npos,
               "D3D12 ImGui backend does not use the 1.92 initialization contract"))
        return false;
    if (!Check(backend.find("ImGui::DestroyPlatformWindows()") == std::string::npos,
               "Editor tears down DX12 platform windows before the renderer backend releases main-viewport data"))
        return false;
    if (!Check(d3d12.find("m_Owner.SetComputeConstants") != std::string::npos &&
                   d3d12.find("SetComputeRootConstantBufferView(0, addr)") != std::string::npos,
               "D3D12 compute bind groups do not bind their reflected b0 constant buffer"))
        return false;
    if (!Check(graphPanel.find("SetNextWindowSize") == std::string::npos,
               "Shader Graph overrides the floating window size instead of preserving its dock rectangle"))
        return false;
    return true;
}

bool TestEditorOperatorSourceContracts() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorOperators.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path())
                break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };
    const auto readSource = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    };
    const auto readSources = [&](std::initializer_list<std::filesystem::path> paths) {
        std::string result;
        for (const std::filesystem::path& path : paths) {
            result += readSource(path);
            result.push_back('\n');
        }
        return result;
    };
    const auto compactSource = [](const std::string& source) {
        std::string compact;
        std::copy_if(source.begin(), source.end(), std::back_inserter(compact),
                     [](unsigned char character) { return std::isspace(character) == 0; });
        return compact;
    };

    const std::filesystem::path root = findRepositoryRoot();
    const std::string contextHeader = readSource(root / "src/Editor/EditorContext.h");
    const std::string panelHeader = readSource(root / "src/Editor/EditorPanel.h");
    const std::string panelSource = readSource(root / "src/Editor/EditorPanel.cpp");
    const std::string operatorsSource = readSources({
        root / "src/Editor/EditorOperators.cpp",
        root / "src/Editor/EditorOperatorShared.h",
        root / "src/Editor/EditorOperatorsAssets.cpp",
        root / "src/Editor/EditorOperatorsCommands.cpp",
        root / "src/Editor/EditorOperatorsComponents.cpp",
        root / "src/Editor/EditorOperatorsDragDrop.cpp",
        root / "src/Editor/EditorOperatorsPrefabs.cpp",
        root / "src/Editor/EditorOperatorsSelection.cpp",
        root / "src/Editor/EditorOperatorsTransactions.cpp",
        root / "src/Editor/EditorOperatorsViewport.cpp",
    });
    const std::string compactOperatorsSource = compactSource(operatorsSource);
    const std::string operatorsHeader = readSource(root / "src/Editor/EditorOperators.h");
    const std::string assetBrowser = readSource(root / "src/Editor/Panels/AssetBrowserPanel.cpp");
    const std::string compactAssetBrowser = compactSource(assetBrowser);
    const std::string hierarchyPanel = readSource(root / "src/Editor/Panels/SceneHierarchyPanel.cpp");
    const std::string compactHierarchyPanel = compactSource(hierarchyPanel);
    const std::string viewportPanel = readSource(root / "src/Editor/Panels/ViewportPanel.cpp");
    const std::string inspectorPanel = readSource(root / "src/Editor/Panels/InspectorPanel.cpp");
    const std::string compactInspectorPanel = compactSource(inspectorPanel);
    const std::string inspectorSections = readSources({
        root / "src/Editor/InspectorSections.cpp",
        root / "src/Editor/InspectorSectionShared.h",
        root / "src/Editor/InspectorSectionsAddComponent.cpp",
        root / "src/Editor/InspectorSectionsAssetScene.cpp",
        root / "src/Editor/InspectorSectionsGameplay.cpp",
        root / "src/Editor/InspectorSectionsPhysics.cpp",
        root / "src/Editor/InspectorSectionsScripting.cpp",
        root / "src/Editor/InspectorSectionsTransformRender.cpp",
        root / "src/Editor/InspectorSectionsUI.cpp",
    });
    const std::string editorCommand = readSource(root / "src/Editor/EditorCommand.cpp");
    const std::string compactEditorCommand = compactSource(editorCommand);
    const std::string editorLayer = readSource(root / "src/Editor/EditorLayer.cpp");
    const std::string compactEditorLayer = compactSource(editorLayer);
    const std::string projectSettings = readSource(root / "src/Editor/EditorProjectSettingsController.cpp");
    const std::string viewportPolicy = readSource(root / "src/Editor/UI/EditorViewportPolicy.h");
    const std::string imguiBackend = readSource(root / "src/Editor/EditorImGuiBackend.cpp");
    const std::string engineSource = readSource(root / "src/Runtime/Core/Engine.cpp");
    const std::string compactEngineSource = compactSource(engineSource);
    const std::string d3d11Context = readSource(root / "src/Runtime/Renderer/D3D11Context.cpp");
    const std::string editorWorkspace = readSource(root / "src/Editor/EditorWorkspace.cpp");
    const std::string shortcutMap = readSource(root / "src/Editor/EditorShortcutMap.cpp");
    const std::string sceneViewportHeader = readSource(root / "src/Runtime/Game/SceneViewportController.h");
    const std::string sceneViewportSource = readSource(root / "src/Runtime/Game/SceneViewportController.cpp");
    const std::string gameViewportSource = readSource(root / "src/Runtime/Game/GameViewport.cpp");
    const std::string uiFacade = readSource(root / "src/Editor/EditorUI/EditorUIFacade.cpp");
    const std::string sceneHeader = readSource(root / "src/Runtime/Scene/Scene.h");
    const std::string actorHeader = readSource(root / "src/Runtime/Scene/Actor.h");
    const std::string sceneSerializer = readSource(root / "src/Runtime/Scene/SceneSerializer.cpp");
    const std::string sceneLighting = readSource(root / "src/Runtime/Renderer/SceneLighting.cpp");
    const std::string prefabAsset = readSource(root / "src/Runtime/Assets/PrefabAsset.cpp");
    const std::string prefabSystem = readSource(root / "src/Runtime/Scene/PrefabSystem.cpp");
    const std::string actorSubtreeSerializer = readSource(root / "src/Runtime/Scene/ActorSubtreeSerializer.cpp");
    if (!Check(!contextHeader.empty() && !panelHeader.empty() && !panelSource.empty() && !operatorsSource.empty() &&
                   !operatorsHeader.empty() && !assetBrowser.empty() && !hierarchyPanel.empty() &&
                   !viewportPanel.empty() && !inspectorPanel.empty() && !sceneViewportHeader.empty() &&
                   !sceneViewportSource.empty() && !gameViewportSource.empty() && !inspectorSections.empty() &&
                   !editorLayer.empty() && !projectSettings.empty() && !viewportPolicy.empty() &&
                   !imguiBackend.empty() && !engineSource.empty() && !d3d11Context.empty() &&
                   !editorWorkspace.empty() && !editorCommand.empty() && !shortcutMap.empty() && !uiFacade.empty() &&
                   !sceneHeader.empty() && !actorHeader.empty() && !sceneSerializer.empty() && !sceneLighting.empty() &&
                   !prefabAsset.empty() && !prefabSystem.empty() && !actorSubtreeSerializer.empty(),
               "operator source contract files were not found"))
        return false;
    auto countSubstring = [](const std::string& source, const std::string& needle) {
        size_t count = 0;
        size_t pos = 0;
        while ((pos = source.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    };
    if (!Check(contextHeader.find("GetOperators()") != std::string::npos &&
                   contextHeader.find("SetWorkspace") != std::string::npos &&
                   contextHeader.find("GetWorkspace") != std::string::npos &&
                   contextHeader.find("SetPanelFocusRequestHandler") != std::string::npos &&
                   contextHeader.find("RequestPanelFocus") != std::string::npos,
               "EditorContext does not expose editor operators"))
        return false;
    if (!Check(panelHeader.find("HandleEditorAction") != std::string::npos &&
                   panelHeader.find("CanHandleEditorAction") != std::string::npos &&
                   panelHeader.find("IsFocused()") != std::string::npos &&
                   panelHeader.find("RequestFocus()") != std::string::npos &&
                   panelSource.find("ImGui::SetNextWindowFocus()") != std::string::npos,
               "EditorPanel does not expose focused action routing"))
        return false;
    if (!Check(editorWorkspace.find("\"panelState\"") != std::string::npos &&
                   editorWorkspace.find("SetPanelStateValue") != std::string::npos &&
                   editorWorkspace.find("GetPanelStateValue") != std::string::npos &&
                   editorWorkspace.find("ClearPanelState") != std::string::npos,
               "EditorWorkspace does not persist panel-local state"))
        return false;
    if (!Check(operatorsHeader.find("DuplicateActorSubtree") != std::string::npos &&
                   operatorsHeader.find("DuplicateSelection") != std::string::npos &&
                   operatorsHeader.find("EditorComponentOperator") != std::string::npos &&
                   operatorsHeader.find("EditorAssetOperator") != std::string::npos &&
                   operatorsHeader.find("CopyAssetToFolder") != std::string::npos &&
                   operatorsHeader.find("CopyAssets") != std::string::npos &&
                   operatorsHeader.find("SceneReferenceInfo") != std::string::npos &&
                   operatorsHeader.find("FindSceneReferences") != std::string::npos &&
                   operatorsHeader.find("FindProjectSceneReferences") != std::string::npos &&
                   operatorsHeader.find("RetargetSceneReferences") != std::string::npos &&
                   operatorsHeader.find("RetargetProjectSceneReferences") != std::string::npos &&
                   operatorsHeader.find("HasActorClipboard") != std::string::npos &&
                   operatorsHeader.find("HasAssetClipboard") != std::string::npos &&
                   operatorsHeader.find("PasteAssetToFolder") != std::string::npos &&
                   operatorsHeader.find("ReimportAll") != std::string::npos &&
                   operatorsHeader.find("EditorPrefabOperator") != std::string::npos &&
                   operatorsHeader.find("CreatePrefabFromActor") != std::string::npos &&
                   operatorsHeader.find("InstantiatePrefab") != std::string::npos &&
                   operatorsHeader.find("SelectInstances") != std::string::npos &&
                   operatorsHeader.find("SetActorTag") != std::string::npos &&
                   operatorsHeader.find("SetActorLayer") != std::string::npos &&
                   operatorsHeader.find("SetActorEditorFlags") != std::string::npos &&
                   operatorsHeader.find("SetActorStatic") != std::string::npos &&
                   operatorsHeader.find("SetActorsActive") != std::string::npos &&
                   operatorsHeader.find("SetActorsTag") != std::string::npos &&
                   operatorsHeader.find("SetActorsLayer") != std::string::npos &&
                   operatorsHeader.find("SetActorsEditorFlags") != std::string::npos &&
                   operatorsHeader.find("SetActorsStatic") != std::string::npos &&
                   operatorsHeader.find("SetActorsPosition") != std::string::npos &&
                   operatorsHeader.find("SetActorsRotation") != std::string::npos &&
                   operatorsHeader.find("SetActorsScale") != std::string::npos &&
                   operatorsHeader.find("AddComponents") != std::string::npos &&
                   operatorsHeader.find("RemoveComponents") != std::string::npos &&
                   operatorsHeader.find("SetComponentPropertyForActors") != std::string::npos &&
                   operatorsHeader.find("SelectActorRange") != std::string::npos &&
                   operatorsHeader.find("SelectActorSubtree") != std::string::npos &&
                   operatorsHeader.find("GetOverrides") != std::string::npos &&
                   operatorsHeader.find("OverrideInfo") != std::string::npos &&
                   operatorsHeader.find("std::string category") != std::string::npos &&
                   operatorsHeader.find("std::string target") != std::string::npos &&
                   operatorsHeader.find("std::string property") != std::string::npos &&
                   operatorsSource.find("OverrideCategory") != std::string::npos &&
                   operatorsSource.find("OverrideCategoryRank") != std::string::npos &&
                   operatorsSource.find("OverrideTargetLabel") != std::string::npos &&
                   operatorsSource.find("OverridePropertyLabel") != std::string::npos &&
                   operatorsSource.find("OverrideValuePreview") != std::string::npos &&
                   operatorsSource.find("std::stable_sort(result.begin(), result.end()") != std::string::npos &&
                   operatorsSource.find("EditorPrefabOperator::CreatePrefabFromActor") != std::string::npos &&
                   operatorsSource.find("EditorPrefabOperator::InstantiatePrefab") != std::string::npos &&
                   operatorsSource.find("EditorPrefabOperator::SelectInstances") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorTag") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorLayer") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorEditorFlags") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorStatic") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsActive") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsTag") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsLayer") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsEditorFlags") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsStatic") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsPosition") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsRotation") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetActorsScale") != std::string::npos &&
                   operatorsSource.find("EditorComponentOperator::AddComponents") != std::string::npos &&
                   operatorsSource.find("EditorComponentOperator::RemoveComponents") != std::string::npos &&
                   operatorsSource.find("EditorComponentOperator::SetComponentPropertyForActors") !=
                       std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::DuplicateSelection") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::CopySelection") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::CopyAssets") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::HasActorClipboard") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::HasAssetClipboard") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::PasteSelection") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::PasteAssetToFolder") != std::string::npos &&
                   operatorsHeader.find("EditorClipboardService m_Clipboard") != std::string::npos &&
                   operatorsSource.find("EditorOperators::EditorOperators()") != std::string::npos &&
                   operatorsSource.find("m_Clipboard->GetAssetPaths()") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::CopyAssetToFolder") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::FindSceneReferences") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::FindProjectSceneReferences") != std::string::npos &&
                   operatorsSource.find("FindAssetReferencesInSceneJson") != std::string::npos &&
                   operatorsSource.find("Find Project Scene References") != std::string::npos &&
                   operatorsSource.find("FindAssetReferencesInJson") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::RetargetSceneReferences") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::RetargetProjectSceneReferences") != std::string::npos &&
                   operatorsSource.find("RetargetAssetReferencesInJson") != std::string::npos &&
                   operatorsSource.find("\"Retarget Asset References\"") != std::string::npos &&
                   operatorsSource.find("\"Retarget Project Scene References\"") != std::string::npos &&
                   operatorsSource.find("CaptureAssetDatabaseRecordsUnderRoot") != std::string::npos &&
                   operatorsSource.find("RemoveAssetDatabaseRecords(commandContext, databaseRecords)") !=
                       std::string::npos &&
                   operatorsSource.find("RestoreAssetDatabaseRecords(commandContext, databaseRecords)") !=
                       std::string::npos &&
                   operatorsSource.find("ModifyAssetsCommand") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::ReimportAll") != std::string::npos &&
                   operatorsSource.find("AssetRenameTargetExists") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::DeleteSelection") != std::string::npos &&
                   operatorsSource.find("GetSelection().GetActorIDs().size() > 1") != std::string::npos &&
                   operatorsSource.find("OrderedSelectedActorRoots(*scene, selectedIDs)") != std::string::npos &&
                   operatorsSource.find("\"Duplicate Actors\"") != std::string::npos &&
                   operatorsSource.find("\"Paste Actors\"") != std::string::npos &&
                   operatorsSource.find("\"roots\"") != std::string::npos &&
                   operatorsSource.find("\"Delete Actors\"") != std::string::npos &&
                   operatorsSource.find("EditorSelectionOperator::SelectActorRange") != std::string::npos &&
                   operatorsSource.find("EditorSelectionOperator::SelectActorSubtree") != std::string::npos &&
                   operatorsSource.find("EditorCommandStack") != std::string::npos &&
                   operatorsSource.find("WatchForChanges()") != std::string::npos &&
                   operatorsSource.find("EditorSelectionOperator::SelectActor") != std::string::npos,
               "EditorOperators does not centralize command/watch/selection behavior"))
        return false;
    const size_t platformBridgePosition = compactEngineSource.find("m_PlatformEventBridge->OnSDLEvent(sdlEvent);");
    const size_t engineWindowFilterPosition =
        compactEngineSource.find("if(engineWindowID!=0&&!IsEngineWindowEvent(sdlEvent,engineWindowID)){continue;}");
    if (!Check(
            assetBrowser.find("registry->WatchForChanges()") == std::string::npos &&
                assetBrowser.find("registry->Refresh()") == std::string::npos &&
                assetBrowser.find("GetAssetRegistry()->Refresh()") == std::string::npos &&
                assetBrowser.find("#include \"Editor/EditorCommand.h\"") == std::string::npos &&
                assetBrowser.find("operators->Assets().WatchIfDue") != std::string::npos &&
                assetBrowser.find("EditorAssetOperator assetOperator") != std::string::npos &&
                assetBrowser.find("assetOperator.Refresh") != std::string::npos &&
                assetBrowser.find("operators->Selection().SelectAsset") != std::string::npos &&
                assetBrowser.find("CreateAssetFromTemplate") != std::string::npos &&
                assetBrowser.find("AssetMatchesBrowserFilters") != std::string::npos &&
                assetBrowser.find("SetOperationMessage") != std::string::npos &&
                assetBrowser.find("m_OperationMessage") != std::string::npos &&
                assetBrowser.find("SetValidationSummary") != std::string::npos &&
                assetBrowser.find("m_ValidationSummaryMessage") != std::string::npos &&
                assetBrowser.find("EditorWidgets::InlineMessage") != std::string::npos &&
                assetBrowser.find("std::vector<EditorAssetInfo> visibleAssets") != std::string::npos &&
                assetBrowser.find("Diagnostics Issues (") != std::string::npos &&
                assetBrowser.find("##AssetDiagnosticsIssues") != std::string::npos &&
                assetBrowser.find("context->RequestPanelFocus(\"inspector\")") != std::string::npos &&
                assetBrowser.find("LoadWorkspaceState") != std::string::npos &&
                assetBrowser.find("SaveWorkspaceState") != std::string::npos &&
                assetBrowser.find("GetPanelStateValue(GetID(), \"filter\")") != std::string::npos &&
                assetBrowser.find("SetPanelStateValue(GetID(), \"selectedFolder\"") != std::string::npos &&
                assetBrowser.find("SetPanelStateValue(GetID(), \"diagnosticsOnly\"") != std::string::npos &&
                assetBrowser.find("Failed to move asset into folder") != std::string::npos &&
                assetBrowser.find("Failed to move folder into folder") != std::string::npos &&
                assetBrowser.find("Moved asset into folder") != std::string::npos &&
                assetBrowser.find("Moved folder into folder") != std::string::npos &&
                assetBrowser.find("MoveSelectedAssetsToFolder") != std::string::npos &&
                assetBrowser.find("rawTargetFolder.is_absolute()") != std::string::npos &&
                assetBrowser.find("FolderPathToAbsolute(targetFolderPath, \"\")") != std::string::npos &&
                assetBrowser.find("Move Selected Assets Here") != std::string::npos &&
                assetBrowser.find("Created asset from template") != std::string::npos &&
                assetBrowser.find("Pasted asset into folder") != std::string::npos &&
                assetBrowser.find("RequestPasteAssetsToFolder") != std::string::npos &&
                compactAssetBrowser.find(
                    "if(ImGui::Selectable(\"PasteIntoFolder\")){RequestPasteAssetsToFolder(*context,folder."
                    "relativePath);") != std::string::npos &&
                assetBrowser.find("resolvedTargetFolder") != std::string::npos &&
                assetBrowser.find("FolderPathToAbsolute(targetFolder, \"\")") != std::string::npos &&
                compactAssetBrowser.find(
                    "operators->Commands().PasteAssetToFolder(context,resolvedTargetFolder.string())") !=
                    std::string::npos &&
                assetBrowser.find("std::filesystem::path(asset.relativePath)") != std::string::npos &&
                assetBrowser.find(".parent_path()") != std::string::npos &&
                countSubstring(assetBrowser, "assetFolder.empty() ? m_SelectedFolder : assetFolder") >= 2 &&
                assetBrowser.find("ImGui::Selectable(\"Move Selected Here\")") != std::string::npos &&
                assetBrowser.find("operators->Commands().PasteAssetToFolder(*context, m_SelectedFolder)") ==
                    std::string::npos &&
                assetBrowser.find("Copied asset:") != std::string::npos &&
                assetBrowser.find("Deleted asset:") != std::string::npos &&
                assetBrowser.find("DrawDeleteReferenceWarning") != std::string::npos &&
                assetBrowser.find("RequestDeleteSelectedAssets") != std::string::npos &&
                compactAssetBrowser.find("if(ImGui::Selectable(\"Delete\")){RequestDeleteSelectedAssets();}") !=
                    std::string::npos &&
                assetBrowser.find("m_PendingDelete = true") != std::string::npos &&
                assetBrowser.find("Delete Assets") != std::string::npos &&
                assetBrowser.find("Cancel Asset Delete") != std::string::npos &&
                assetBrowser.find("Delete %zu selected asset(s)?") == std::string::npos &&
                assetBrowser.find("CollectAssetPathsForFolder") != std::string::npos &&
                assetBrowser.find("m_PendingFolderDelete") != std::string::npos &&
                assetBrowser.find("m_PendingFolderDeletePath") != std::string::npos &&
                assetBrowser.find("RequestDeleteFolder(const std::string& folderPath)") != std::string::npos &&
                assetBrowser.find("RequestDeleteSelectedFolder") != std::string::npos &&
                assetBrowser.find("RequestDeleteFolder(folder.relativePath)") != std::string::npos &&
                assetBrowser.find("RequestCreateFolderInFolder(*context, folder.relativePath, true)") !=
                    std::string::npos &&
                compactAssetBrowser.find("if(ImGui::BeginPopup(\"##FolderCtx\")){if(ImGui::Selectable(\"Refresh\"))") !=
                    std::string::npos &&
                assetBrowser.find("const std::string folderPath = !m_PendingFolderDeletePath.empty()") !=
                    std::string::npos &&
                assetBrowser.find("CountSceneReferencesForAssets") != std::string::npos &&
                assetBrowser.find("CountProjectSceneReferencesForAssets") != std::string::npos &&
                assetBrowser.find("CountProjectSceneReferencesForFolder") != std::string::npos &&
                assetBrowser.find("AppendRetargetReferenceWarning") != std::string::npos &&
                assetBrowser.find("projectReferenceCount") != std::string::npos &&
                assetBrowser.find("m_PendingAssetRetargets") != std::string::npos &&
                assetBrowser.find("BuildPendingRetargetsForFolder") != std::string::npos &&
                assetBrowser.find("SetPendingRetargets") != std::string::npos &&
                assetBrowser.find("ExecutePendingRetargets") != std::string::npos &&
                assetBrowser.find("Retarget References") != std::string::npos &&
                assetBrowser.find("operators->Assets().RetargetSceneReferences") != std::string::npos &&
                assetBrowser.find("operators->Assets().RetargetProjectSceneReferences") != std::string::npos &&
                assetBrowser.find("Retargeted ") != std::string::npos &&
                assetBrowser.find("current / %zu project scene reference(s)") != std::string::npos &&
                assetBrowser.find("retarget.projectReferenceCount > 0") != std::string::npos &&
                assetBrowser.find("operators->Assets().FindSceneReferences") != std::string::npos &&
                assetBrowser.find("operators->Assets().FindProjectSceneReferences") != std::string::npos &&
                assetBrowser.find("current/project scene reference(s) will become unresolved") != std::string::npos &&
                assetBrowser.find("project scene reference(s)") != std::string::npos &&
                assetBrowser.find("still point to old asset path(s)") != std::string::npos &&
                assetBrowser.find("AppendRetargetReferenceWarning(\"Renamed asset to: \" + newName") !=
                    std::string::npos &&
                assetBrowser.find("AppendRetargetReferenceWarning(\"Renamed folder to: \" + newName") !=
                    std::string::npos &&
                compactAssetBrowser.find("AppendRetargetReferenceWarning(\"Movedassetintofolder:\"") !=
                    std::string::npos &&
                compactAssetBrowser.find("AppendRetargetReferenceWarning(\"Movedfolderintofolder:\"") !=
                    std::string::npos &&
                assetBrowser.find("RequestRevealPath") != std::string::npos &&
                assetBrowser.find("Opened asset:") != std::string::npos &&
                assetBrowser.find("Failed to open asset:") != std::string::npos &&
                assetBrowser.find("Revealed in Explorer") != std::string::npos &&
                assetBrowser.find("Failed to reveal in Explorer") != std::string::npos &&
                assetBrowser.find("Refreshed asset registry") != std::string::npos &&
                assetBrowser.find("RequestValidateAssets") != std::string::npos &&
                assetBrowser.find("operators->Commands().ExecuteAction(*context, \"asset.validate\")") !=
                    std::string::npos &&
                assetBrowser.find("GetService<EditorImportService>") != std::string::npos &&
                assetBrowser.find("GetValidationSummaryText()") != std::string::npos &&
                assetBrowser.find("HasValidationIssues()") != std::string::npos &&
                assetBrowser.find("RefreshValidation") == std::string::npos &&
                assetBrowser.find("Validated assets; diagnostics are refreshed.") != std::string::npos &&
                assetBrowser.find("Validate Assets") != std::string::npos &&
                assetBrowser.find("Reimported asset:") != std::string::npos &&
                assetBrowser.find("operators->Assets().ReimportAll") != std::string::npos &&
                assetBrowser.find("Reimport completed with failures") != std::string::npos &&
                assetBrowser.find("AssetTypeFromFilterIndex") != std::string::npos &&
                assetBrowser.find("m_RecursiveAssets") != std::string::npos &&
                assetBrowser.find("m_DiagnosticsOnly") != std::string::npos &&
                assetBrowser.find("m_TypeFilter") != std::string::npos &&
                assetBrowser.find("m_ImportStateFilter") != std::string::npos &&
                compactAssetBrowser.find("GetAssetsInFolder(m_SelectedFolder,m_RecursiveAssets") != std::string::npos &&
                assetBrowser.find("SaveMaterialAssetToFile") == std::string::npos &&
                assetBrowser.find("std::ofstream") == std::string::npos &&
                assetBrowser.find("operators->Assets().OpenAsset") != std::string::npos &&
                assetBrowser.find("operators->Assets().RevealAsset") != std::string::npos &&
                assetBrowser.find("RequestOpenAsset") != std::string::npos &&
                assetBrowser.find("RequestOpenFolder") != std::string::npos &&
                assetBrowser.find("RequestOpenFolder(folder.relativePath)") != std::string::npos &&
                assetBrowser.find("Opened folder: \" + m_SelectedFolder") != std::string::npos &&
                compactAssetBrowser.find(
                    "SaveWorkspaceState();SetOperationMessage(\"Openedfolder:\"+m_SelectedFolder") !=
                    std::string::npos &&
                assetBrowser.find("DrawPendingSceneOpenModal") != std::string::npos &&
                assetBrowser.find("Unsaved Scene Changes###asset_open_dirty_scene") != std::string::npos &&
                assetBrowser.find("layer->SaveScene()") != std::string::npos &&
                assetBrowser.find("RequestSaveScene") != std::string::npos &&
                assetBrowser.find("OpenPendingSceneAsset(true)") != std::string::npos &&
                compactAssetBrowser.find("if(!opened&&discardUnsavedChanges)layer->MarkDirty();") !=
                    std::string::npos &&
                assetBrowser.find("HandleEditorAction") != std::string::npos &&
                assetBrowser.find("CanHandleEditorAction") != std::string::npos &&
                assetBrowser.find("bool AssetBrowserPanel::StartRenameSelectedAsset()") != std::string::npos &&
                assetBrowser.find("return StartRenameSelectedAsset();") != std::string::npos &&
                assetBrowser.find("AssetParentFolderForBrowser(*context, path)") != std::string::npos &&
                assetBrowser.find("m_Filter[0] = '\\0';") != std::string::npos &&
                assetBrowser.find("m_RecursiveAssets = true;") != std::string::npos &&
                assetBrowser.find("edit.delete") != std::string::npos &&
                assetBrowser.find("edit.duplicate") != std::string::npos &&
                assetBrowser.find("edit.copy") != std::string::npos &&
                assetBrowser.find("edit.paste") != std::string::npos &&
                assetBrowser.find("asset.open") != std::string::npos &&
                assetBrowser.find("asset.reveal") != std::string::npos &&
                assetBrowser.find("asset.createFolder") != std::string::npos &&
                assetBrowser.find("RequestCreateFolderInFolder") != std::string::npos &&
                assetBrowser.find("RequestCreateAssetFromTemplateInFolder") != std::string::npos &&
                assetBrowser.find("TemplateTargetDirectoryInFolder") != std::string::npos &&
                assetBrowser.find("bool selectCreated") != std::string::npos &&
                assetBrowser.find("RequestCreateFolderInFolder(context, m_SelectedFolder, false)") !=
                    std::string::npos &&
                assetBrowser.find("RequestCreateFolderInFolder(*context, folder.relativePath, true)") !=
                    std::string::npos &&
                compactAssetBrowser.find(
                    "RequestCreateAssetFromTemplateInFolder(*context,folder.relativePath,\"material\")") !=
                    std::string::npos &&
                compactAssetBrowser.find(
                    "RequestCreateAssetFromTemplateInFolder(*context,folder.relativePath,\"scene\")") !=
                    std::string::npos &&
                compactAssetBrowser.find(
                    "m_SelectedFolder=(std::filesystem::path(parentFolder)/folderPath.filename())") !=
                    std::string::npos &&
                assetBrowser.find("SaveWorkspaceState();") != std::string::npos &&
                assetBrowser.find("asset.createMaterial") != std::string::npos &&
                assetBrowser.find("asset.createTexture") != std::string::npos &&
                assetBrowser.find("asset.createPrefab") != std::string::npos &&
                assetBrowser.find("asset.createAngelScript") != std::string::npos &&
                assetBrowser.find("asset.createLua") != std::string::npos &&
                assetBrowser.find("asset.createShader") != std::string::npos &&
                assetBrowser.find("asset.createUI") != std::string::npos &&
                assetBrowser.find("asset.createScene") != std::string::npos &&
                assetBrowser.find("asset.move") != std::string::npos &&
                assetBrowser.find("asset.rename") != std::string::npos &&
                countSubstring(compactAssetBrowser,
                               "(IsFocused()&&!m_SelectedFolder.empty()&&!IsRootFolder(m_SelectedFolder))") >= 2 &&
                assetBrowser.find("ImGui::MenuItem(\"Material\")") != std::string::npos &&
                assetBrowser.find("ImGui::MenuItem(\"Default Texture\")") != std::string::npos &&
                assetBrowser.find("TemplateTargetDirectoryInFolder(folderPath, templateID)") != std::string::npos &&
                assetBrowser.find("createTemplateFromToolbar(\"material\")") != std::string::npos &&
                assetBrowser.find("createTemplateFromToolbar(\"texture\")") != std::string::npos &&
                assetBrowser.find("operators->Assets().MoveAsset") != std::string::npos &&
                assetBrowser.find("PasteAssetToFolder") != std::string::npos &&
                assetBrowser.find("edit.selectAll") != std::string::npos &&
                assetBrowser.find("m_SelectedAssetPaths") != std::string::npos &&
                assetBrowser.find("SelectAssetRow") != std::string::npos &&
                assetBrowser.find("SelectVisibleAssets") != std::string::npos &&
                assetBrowser.find("ActiveSelectedAssetPaths") != std::string::npos &&
                assetBrowser.find("SyncAssetSelectionFromContext") != std::string::npos &&
                assetBrowser.find("io.KeyCtrl") != std::string::npos &&
                assetBrowser.find("io.KeyShift") != std::string::npos &&
                assetBrowser.find("Deleted \" + std::to_string(deletedCount)") != std::string::npos &&
                assetBrowser.find("Duplicated \" + std::to_string(duplicatedCount)") != std::string::npos &&
                assetBrowser.find("Moved \" + std::to_string(movedCount)") != std::string::npos &&
                assetBrowser.find("ReimportSelectedAssets") != std::string::npos &&
                assetBrowser.find("registry->GetAssetInfo(path)") != std::string::npos &&
                assetBrowser.find("info->imported") != std::string::npos &&
                assetBrowser.find("operators->Assets().Reimport(*context, info->uuid)") != std::string::npos &&
                assetBrowser.find("Reimport Selected") != std::string::npos &&
                assetBrowser.find("Reimported \" + std::to_string(reimportedCount)") != std::string::npos &&
                assetBrowser.find("No imported assets selected for reimport") != std::string::npos &&
                assetBrowser.find("operators->Commands().CopyAssets") != std::string::npos &&
                assetBrowser.find("Copied \" + std::to_string(selectedPaths.size())") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Open Selected\")") != std::string::npos &&
                assetBrowser.find("RequestOpenAsset(emptyContextSelection.front())") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Reveal Selected in Explorer\")") != std::string::npos &&
                assetBrowser.find("RequestRevealPath(emptyContextSelection.front())") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Reimport Selected\")") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Copy Selected\")") != std::string::npos &&
                assetBrowser.find("operators->Commands().CopyAssets(*context, emptyContextSelection)") !=
                    std::string::npos &&
                assetBrowser.find("MoveSelectedAssetsToFolder(*context, m_SelectedFolder)") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Duplicate Selected\")") != std::string::npos &&
                assetBrowser.find("ImGui::Selectable(\"Delete Selected\")") != std::string::npos &&
                assetBrowser.find("kFolderPayload") != std::string::npos &&
                assetBrowser.find("MoveFolder") != std::string::npos &&
                assetBrowser.find("RenameFolder") != std::string::npos &&
                assetBrowser.find("AcceptDragDropPayload(kFolderPayload)") != std::string::npos &&
                assetBrowser.find("FolderPathToAbsolute(folder.relativePath, \"\")") != std::string::npos &&
                compactAssetBrowser.find("operators->Assets().MoveAsset(*context,source,targetFolder.string())") !=
                    std::string::npos &&
                assetBrowser.find("!StartsWithPath(folder.relativePath, source)") != std::string::npos &&
                compactAssetBrowser.find("if(!IsRootFolder(folder.relativePath)){if(constImGuiPayload*payload="
                                         "ImGui::AcceptDragDropPayload(kFolderPayload))") == std::string::npos,
            "AssetBrowser still bypasses operator watch/selection/create/open paths"))
        return false;
    if (!Check(operatorsSource.find("EditorAssetOperator::OpenAsset") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::RevealAsset") != std::string::npos &&
                   operatorsSource.find("Refusing to open scene asset with unsaved changes") != std::string::npos &&
                   operatorsSource.find("layer->IsDirty()") != std::string::npos &&
                   operatorsSource.find("OpenExternalFile") != std::string::npos &&
                   compactOperatorsSource.find("if(type!=EditorAssetType::Unknown)returnfalse") != std::string::npos &&
                   operatorsSource.find("context.RequestPanelFocus(\"inspector\")") != std::string::npos &&
                   operatorsSource.find("RevealExternalPath") != std::string::npos &&
                   operatorsSource.find("ShellExecuteW") != std::string::npos &&
                   operatorsSource.find("_wsystem") == std::string::npos,
               "asset open/reveal workflow should use operator-owned Shell API helpers"))
        return false;
    if (!Check(editorCommand.find("CaptureAssetDatabaseRecordsForPath") != std::string::npos &&
                   editorCommand.find("m_RemovedDatabaseRecords = CaptureAssetDatabaseRecordsForPath") !=
                       std::string::npos &&
                   editorCommand.find("RemoveAssetDatabaseRecords(context, m_RemovedDatabaseRecords)") !=
                       std::string::npos &&
                   editorCommand.find("RestoreAssetDatabaseRecords(context, m_RemovedDatabaseRecords)") !=
                       std::string::npos,
               "asset delete commands must remove and restore AssetDatabase records"))
        return false;
    if (!Check(hierarchyPanel.find("CanHandleEditorAction") != std::string::npos &&
                   hierarchyPanel.find("edit.delete") != std::string::npos &&
                   hierarchyPanel.find("edit.duplicate") != std::string::npos &&
                   hierarchyPanel.find("edit.copy") != std::string::npos &&
                   hierarchyPanel.find("edit.paste") != std::string::npos &&
                   hierarchyPanel.find("HasActorClipboard") != std::string::npos &&
                   hierarchyPanel.find("DuplicateSelection") != std::string::npos &&
                   hierarchyPanel.find("DeleteSelection") != std::string::npos &&
                   hierarchyPanel.find("CopySelection") != std::string::npos &&
                   hierarchyPanel.find("PasteSelection") != std::string::npos &&
                   hierarchyPanel.find("Commands().CreateChildActor") != std::string::npos &&
                   compactHierarchyPanel.find(
                       "CreateActor(*context,\"Actor\");if(id)operators->Commands().MoveActor") == std::string::npos &&
                   hierarchyPanel.find("Commands().DeleteActor") != std::string::npos &&
                   hierarchyPanel.find("edit.selectAll") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.expandAll") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.collapseAll") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.createEmptyParent") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.unparent") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.moveUp") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.moveDown") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.selectChildren") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.selectSubtree") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.selectParent") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.selectPreviousSibling") != std::string::npos &&
                   hierarchyPanel.find("hierarchy.selectNextSibling") != std::string::npos &&
                   hierarchyPanel.find("Commands().CreateEmptyParent") != std::string::npos &&
                   hierarchyPanel.find("Commands().UnparentActor") != std::string::npos &&
                   hierarchyPanel.find("Commands().MoveActorUp") != std::string::npos &&
                   hierarchyPanel.find("Commands().MoveActorDown") != std::string::npos &&
                   hierarchyPanel.find("Select Children") != std::string::npos &&
                   hierarchyPanel.find("Select Subtree") != std::string::npos &&
                   hierarchyPanel.find("Select Parent") != std::string::npos &&
                   hierarchyPanel.find("Select Previous Sibling") != std::string::npos &&
                   hierarchyPanel.find("Select Next Sibling") != std::string::npos &&
                   hierarchyPanel.find("Selection().SelectActorSubtree") != std::string::npos &&
                   hierarchyPanel.find("EditorDragDropOperator dragDropOperator") != std::string::npos &&
                   hierarchyPanel.find("dragDropOperator.ApplyActorDrop") != std::string::npos &&
                   hierarchyPanel.find("#include \"Editor/EditorCommand.h\"") == std::string::npos &&
                   hierarchyPanel.find("MakeMoveActorCommand") == std::string::npos &&
                   hierarchyPanel.find("GetCommandStack()->ExecuteCommand") == std::string::npos &&
                   hierarchyPanel.find("CreateEmptyParentForSelection") == std::string::npos &&
                   hierarchyPanel.find("UnparentSelectedActor") == std::string::npos &&
                   hierarchyPanel.find("MoveSelectedActorUp") == std::string::npos &&
                   hierarchyPanel.find("MoveSelectedActorDown") == std::string::npos &&
                   compactHierarchyPanel.find("MakeSceneSnapshotCommand(\"CreateEmptyParent\"") == std::string::npos &&
                   hierarchyPanel.find("Selection().SelectActor") != std::string::npos &&
                   hierarchyPanel.find("SelectActorRange") != std::string::npos &&
                   hierarchyPanel.find("m_LastClickedActorID") != std::string::npos &&
                   hierarchyPanel.find("SetNextItemOpen") != std::string::npos &&
                   hierarchyPanel.find("m_TagFilter") != std::string::npos &&
                   hierarchyPanel.find("m_LayerFilterEnabled") != std::string::npos &&
                   hierarchyPanel.find("m_ComponentFilter") != std::string::npos &&
                   hierarchyPanel.find("HasHierarchyFilters") != std::string::npos &&
                   hierarchyPanel.find("ActorMatchesOwnFilters") != std::string::npos &&
                   hierarchyPanel.find("actor.ForEachComponent") != std::string::npos &&
                   hierarchyPanel.find("DrawActorFilterMatchHighlight") != std::string::npos &&
                   hierarchyPanel.find("ActorMatchesOwnFilters(*actor)") != std::string::npos &&
                   hierarchyPanel.find("Commands().CreateUIActor") != std::string::npos &&
                   hierarchyPanel.find("UIActorPreset") == std::string::npos &&
                   hierarchyPanel.find("AddUIPresetComponents") == std::string::npos &&
                   hierarchyPanel.find("scene.CreateActor(UIActorName") == std::string::npos &&
                   hierarchyPanel.find("SelectActorID") == std::string::npos &&
                   hierarchyPanel.find("AddToMultiSelect") == std::string::npos,
               "SceneHierarchyPanel still bypasses operator duplicate/delete paths"))
        return false;
    if (!Check(operatorsHeader.find("CreateChildActor") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::CreateChildActor") != std::string::npos &&
                   operatorsSource.find("desc.parent = parent->GetHandle()") != std::string::npos,
               "child actor creation is not centralized in EditorCommandOperator"))
        return false;
    if (!Check(operatorsHeader.find("CreateEmptyParent") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::CreateEmptyParent") != std::string::npos &&
                   operatorsSource.find("\"Create Empty Parent\"") != std::string::npos,
               "empty parent creation is not centralized in EditorCommandOperator"))
        return false;
    if (!Check(operatorsHeader.find("UnparentActor") != std::string::npos &&
                   operatorsHeader.find("MoveActorUp") != std::string::npos &&
                   operatorsHeader.find("MoveActorDown") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::UnparentActor") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::MoveActorUp") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::MoveActorDown") != std::string::npos,
               "hierarchy organization commands are not centralized in EditorCommandOperator"))
        return false;
    if (!Check(operatorsHeader.find("CreateUIActor") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::CreateUIActor") != std::string::npos &&
                   operatorsSource.find("AddUIPresetComponents") != std::string::npos &&
                   operatorsSource.find("Create UI Actor") != std::string::npos,
               "UI actor preset creation is not centralized in EditorCommandOperator"))
        return false;
    if (!Check(hierarchyPanel.find("Prefabs().CreatePrefabFromActor") != std::string::npos &&
                   hierarchyPanel.find("Prefabs().InstantiatePrefab") != std::string::npos &&
                   hierarchyPanel.find("PrefabSystem::SaveSubtree") == std::string::npos &&
                   hierarchyPanel.find("PrefabSystem::Instantiate") == std::string::npos &&
                   hierarchyPanel.find("MakeSceneSnapshotCommand(\"Instantiate Prefab\"") == std::string::npos &&
                   viewportPanel.find("Prefabs().InstantiatePrefab") != std::string::npos &&
                   viewportPanel.find("PrefabSystem::Instantiate") == std::string::npos &&
                   viewportPanel.find("MakeSceneSnapshotCommand(\"Drop Prefab\"") == std::string::npos,
               "prefab create/drop paths are not centralized in EditorPrefabOperator"))
        return false;
    if (!Check(inspectorPanel.find("Prefabs().ApplyAll") != std::string::npos &&
                   inspectorPanel.find("Prefabs().GetOverrides") != std::string::npos &&
                   inspectorPanel.find("Prefabs().ApplyOverride") != std::string::npos &&
                   inspectorPanel.find("Prefabs().RevertOverride") != std::string::npos &&
                   inspectorPanel.find("Selection().SelectAsset") != std::string::npos &&
                   inspectorPanel.find("hasPrefabOverrides") != std::string::npos &&
                   inspectorPanel.find("ImGui::BeginDisabled(!operators || !hasPrefabOverrides)") !=
                       std::string::npos &&
                   inspectorPanel.find("ImGui::BeginDisabled(!operators)") != std::string::npos &&
                   inspectorPanel.find("ImGui::Button(\"Unpack\")") != std::string::npos &&
                   inspectorPanel.find("Selected prefab source") != std::string::npos &&
                   inspectorPanel.find("Failed to select prefab source") != std::string::npos &&
                   inspectorPanel.find("TableSetupColumn(\"Category\")") != std::string::npos &&
                   inspectorPanel.find("TableSetupColumn(\"Property\")") != std::string::npos &&
                   inspectorPanel.find("TableSetupColumn(\"Preview\")") != std::string::npos &&
                   inspectorPanel.find("TableSetupColumn(\"Status\")") != std::string::npos &&
                   inspectorPanel.find("ImGui::TextDisabled(\"Ready\")") != std::string::npos &&
                   inspectorPanel.find("ImGui::TextDisabled(\"%s\", item.diagnostic.c_str())") != std::string::npos &&
                   inspectorPanel.find("##PrefabOverrideFilter") != std::string::npos &&
                   inspectorPanel.find("m_PrefabOverrideFilter") != std::string::npos &&
                   inspectorPanel.find("m_PrefabOverrideDiagnosticsOnly") != std::string::npos &&
                   inspectorPanel.find("m_PrefabOverrideCategoryOpen") != std::string::npos &&
                   inspectorPanel.find("m_PrefabOperationMessage") != std::string::npos &&
                   inspectorPanel.find("setPrefabOperationMessage") != std::string::npos &&
                   compactInspectorPanel.find("directCommand=refreshed") == std::string::npos &&
                   compactInspectorPanel.find("directCommand=directCommand||refreshed") != std::string::npos &&
                   inspectorPanel.find("Failed to apply prefab override") != std::string::npos &&
                   inspectorPanel.find("Failed to revert prefab override") != std::string::npos &&
                   inspectorPanel.find("EditorWidgets::InlineMessage") != std::string::npos &&
                   inspectorPanel.find("Diagnostics only") != std::string::npos &&
                   inspectorPanel.find("visibleOverrideCount") != std::string::npos &&
                   inspectorPanel.find("visibleReadyCount") != std::string::npos &&
                   inspectorPanel.find("visibleBlockedCount") != std::string::npos &&
                   inspectorPanel.find("%d shown / %d total - %d ready, %d blocked") != std::string::npos &&
                   inspectorPanel.find("ImGuiHoveredFlags_AllowWhenDisabled") != std::string::npos &&
                   inspectorPanel.find("This override kind cannot be applied individually.") != std::string::npos &&
                   inspectorPanel.find("This override kind cannot be reverted individually.") != std::string::npos &&
                   inspectorPanel.find("categoryCounts") != std::string::npos &&
                   inspectorPanel.find("PrefabOverrideGroup") != std::string::npos &&
                   inspectorPanel.find("std::to_string(categoryCounts") != std::string::npos &&
                   inspectorPanel.find("ImGuiSelectableFlags_SpanAllColumns") != std::string::npos &&
                   inspectorPanel.find("No overrides match the current filter") != std::string::npos &&
                   inspectorPanel.find("item.category") != std::string::npos &&
                   inspectorPanel.find("item.target") != std::string::npos &&
                   inspectorPanel.find("item.property") != std::string::npos &&
                   inspectorPanel.find("std::string currentCategory") != std::string::npos &&
                   inspectorPanel.find("ImGui::Selectable(groupLabel.c_str()") != std::string::npos &&
                   inspectorPanel.find("CommitInspectorActorName") != std::string::npos &&
                   inspectorPanel.find("CommitInspectorActorActive") != std::string::npos &&
                   inspectorPanel.find("CommitInspectorActorTag") != std::string::npos &&
                   inspectorPanel.find("CommitInspectorActorLayer") != std::string::npos &&
                   inspectorPanel.find("CommitInspectorActorStatic") != std::string::npos &&
                   inspectorPanel.find("multiActorSelection") != std::string::npos &&
                   inspectorPanel.find("SetActorsActive") != std::string::npos &&
                   inspectorPanel.find("SetActorsTag") != std::string::npos &&
                   inspectorPanel.find("SetActorsLayer") != std::string::npos &&
                   inspectorPanel.find("CommonStaticState") != std::string::npos &&
                   inspectorPanel.find("SetActorsStatic") != std::string::npos &&
                   inspectorPanel.find("ImGui::Checkbox(\"Static\"") != std::string::npos &&
                   inspectorPanel.find("CommonPositionValue") != std::string::npos &&
                   inspectorPanel.find("SetActorsPosition") != std::string::npos &&
                   inspectorPanel.find("CommonRotationValue") != std::string::npos &&
                   inspectorPanel.find("SetActorsRotation") != std::string::npos &&
                   inspectorPanel.find("CommonScaleValue") != std::string::npos &&
                   inspectorPanel.find("SetActorsScale") != std::string::npos &&
                   inspectorPanel.find("CommonComponentTypes") != std::string::npos &&
                   inspectorPanel.find("Add Component to Selection") != std::string::npos &&
                   inspectorPanel.find("Components().AddComponents") != std::string::npos &&
                   inspectorPanel.find("Components().RemoveComponents") != std::string::npos &&
                   inspectorPanel.find("CommonComponentProperties") != std::string::npos &&
                   inspectorPanel.find("SetComponentPropertyForActors") != std::string::npos &&
                   inspectorPanel.find("#include \"Editor/EditorCommand.h\"") == std::string::npos &&
                   inspectorPanel.find("#include <fstream>") == std::string::npos &&
                   inspectorPanel.find("ReadTextFile") == std::string::npos &&
                   inspectorPanel.find("WriteTextFile") == std::string::npos &&
                   inspectorPanel.find("std::ofstream") == std::string::npos &&
                   inspectorPanel.find("LambdaEditorCommand") == std::string::npos &&
                   inspectorPanel.find("actor.SetName") == std::string::npos &&
                   inspectorPanel.find("actor.SetActive") == std::string::npos &&
                   inspectorPanel.find("actor.SetTag") == std::string::npos &&
                   inspectorPanel.find("actor.SetLayer") == std::string::npos &&
                   inspectorPanel.find("actor.SetStatic") == std::string::npos &&
                   inspectorPanel.find("actor.SetEditorFlags") == std::string::npos &&
                   inspectorPanel.find("actor->SetName") == std::string::npos &&
                   inspectorPanel.find("actor->SetTag") == std::string::npos &&
                   inspectorPanel.find("actor->SetLayer") == std::string::npos &&
                   inspectorPanel.find("actor->SetStatic") == std::string::npos &&
                   inspectorPanel.find("actor->SetEditorFlags") == std::string::npos &&
                   operatorsSource.find("\"RemoveActorSubtree\"") != std::string::npos &&
                   operatorsSource.find("\"AddActorSubtree\"") != std::string::npos &&
                   operatorsSource.find("\"AddComponent\"") != std::string::npos &&
                   operatorsSource.find("\"RemoveComponent\"") != std::string::npos &&
                   inspectorPanel.find("PrefabSystem::ApplyAll") == std::string::npos,
               "InspectorPanel still bypasses prefab or actor-name operator paths"))
        return false;
    if (!Check(actorHeader.find("GetEditorFlags") != std::string::npos &&
                   actorHeader.find("SetEditorFlags") != std::string::npos &&
                   actorHeader.find("IsStatic") != std::string::npos &&
                   actorHeader.find("SetStatic") != std::string::npos &&
                   sceneSerializer.find("\"editorFlags\"") != std::string::npos &&
                   sceneSerializer.find("QueueSetEditorFlags") != std::string::npos &&
                   prefabAsset.find("\"editorFlags\"") != std::string::npos &&
                   prefabSystem.find("rootDesc.editorFlags") != std::string::npos &&
                   prefabSystem.find("desc.editorFlags") != std::string::npos &&
                   prefabSystem.find("\"/editorFlags\"") != std::string::npos &&
                   actorSubtreeSerializer.find("node.editorFlags = actor.GetEditorFlags()") != std::string::npos &&
                   editorCommand.find("desc.editorFlags") != std::string::npos,
               "actor static/editor flags are not preserved across scene, prefab, and subtree serialization"))
        return false;
    if (!Check(inspectorSections.find("RemoveComponentByType(context") != std::string::npos &&
                   inspectorSections.find("Components().SetProperty") != std::string::npos &&
                   inspectorSections.find("AddComponentByType(context") != std::string::npos &&
                   inspectorSections.find("componentOperator.AddComponent") != std::string::npos &&
                   inspectorSections.find("componentOperator.RemoveComponent") != std::string::npos &&
                   inspectorSections.find("componentOperator.SetProperty") != std::string::npos &&
                   inspectorSections.find("Search components") != std::string::npos &&
                   inspectorSections.find("Recently Used") != std::string::npos &&
                   inspectorSections.find("ComponentCategory") != std::string::npos &&
                   inspectorSections.find("RecordRecentComponent") != std::string::npos &&
                   inspectorSections.find("ComponentMatchesFilter") != std::string::npos &&
                   inspectorSections.find("GetPanelStateValue(") != std::string::npos &&
                   inspectorSections.find("SetPanelStateValue(") != std::string::npos &&
                   inspectorSections.find("kRecentComponentsKey") != std::string::npos &&
                   inspectorSections.find("m_RecentComponentsLoaded") != std::string::npos &&
                   inspectorSections.find("EditorViewportPolicy::BindNextPopupToCurrentViewport()") !=
                       std::string::npos &&
                   inspectorSections.find("ImGui::BeginCombo(\"##AddComponent\"") != std::string::npos &&
                   inspectorSections.find("ImGui::BeginMenu(category)") != std::string::npos &&
                   inspectorPanel.find("EditorViewportPolicy::BindNextPopupToCurrentViewport()") != std::string::npos &&
                   inspectorPanel.find("ImGui::BeginCombo(\"##MultiAddComponent\"") != std::string::npos &&
                   inspectorPanel.find("ImGui::BeginMenu(category)") != std::string::npos &&
                   inspectorSections.find("std::make_unique<SetComponentPropertyCommand>") == std::string::npos &&
                   inspectorSections.find("context.MarkSceneDirty()") == std::string::npos &&
                   inspectorSections.find("std::make_unique<AddComponentCommand>") == std::string::npos &&
                   inspectorSections.find("actor.RemoveComponentByTypeName") == std::string::npos &&
                   inspectorSections.find("actor->AddComponent<ScriptComponent>()") == std::string::npos,
               "InspectorSections do not route component edits through operators"))
        return false;
    if (!Check(
            viewportPolicy.find("ImGui::GetMainViewport()") != std::string::npos &&
                viewportPolicy.find("ImGui::GetWindowViewport()") != std::string::npos &&
                viewportPolicy.find("BindNextPopupToViewport(uint32_t viewportID)") != std::string::npos &&
                compactAssetBrowser.find(
                    "constuint32_tassetBrowserViewportID=Editor::UI::EditorViewportPolicy::GetCurrentViewportID();") !=
                    std::string::npos &&
                countSubstring(compactAssetBrowser, "Editor::UI::EditorViewportPolicy::BindNextPopupToViewport("
                                                    "assetBrowserViewportID);") == 6 &&
                editorLayer.find("EditorViewportPolicy::BindNextModalToMainViewport();\n"
                                 "    if (!ImGui::BeginPopupModal(\"Project Result\"") != std::string::npos &&
                projectSettings.find("EditorViewportPolicy::BindNextModalToMainViewport();\n"
                                     "    if (!ImGui::BeginPopupModal(\"Settings\"") != std::string::npos &&
                assetBrowser.find("EditorViewportPolicy::BindNextModalToMainViewport();\n"
                                  "    if (!ImGui::BeginPopupModal(\"Unsaved Scene Changes") != std::string::npos &&
                imguiBackend.find("class D3D11ImmediateContextStateScope") != std::string::npos &&
                imguiBackend.find("D3D11ImmediateContextStateScope stateScope(") != std::string::npos &&
                imguiBackend.find("m_Context->OMGetRenderTargets(1, &m_RenderTarget, &m_DepthStencil)") !=
                    std::string::npos &&
                imguiBackend.find("m_Context->RSGetViewports(&m_ViewportCount, m_Viewports)") != std::string::npos &&
                imguiBackend.find("m_Context->RSGetScissorRects(&m_ScissorCount, m_Scissors)") != std::string::npos &&
                imguiBackend.find("m_Context->OMSetRenderTargets(1, &m_RenderTarget, m_DepthStencil)") !=
                    std::string::npos &&
                imguiBackend.find("m_Context->RSSetViewports(m_ViewportCount, m_Viewports)") != std::string::npos &&
                imguiBackend.find("m_Context->RSSetScissorRects(m_ScissorCount, m_Scissors)") != std::string::npos &&
                imguiBackend.find("m_RenderTarget->Release()") != std::string::npos &&
                imguiBackend.find("m_DepthStencil->Release()") != std::string::npos &&
                engineSource.find("SDL_GetWindowID(m_Window->GetSDLWindow())") != std::string::npos &&
                engineSource.find("return event.key.windowID == engineWindowID") != std::string::npos &&
                engineSource.find("return event.window.windowID == engineWindowID") != std::string::npos &&
                engineSource.find("return event.button.windowID == engineWindowID") != std::string::npos &&
                engineSource.find("return event.motion.windowID == engineWindowID") != std::string::npos &&
                engineSource.find("return event.wheel.windowID == engineWindowID") != std::string::npos &&
                engineSource.find("return event.text.windowID == engineWindowID") != std::string::npos &&
                platformBridgePosition != std::string::npos && engineWindowFilterPosition != std::string::npos &&
                platformBridgePosition < engineWindowFilterPosition &&
                imguiBackend.find("ctx->RSSetViewports(1, &viewport)") != std::string::npos &&
                imguiBackend.find("ctx->RSSetScissorRects(1, &scissor)") != std::string::npos &&
                d3d11Context.find("m_Context->RSSetViewports(1, &viewport)") != std::string::npos &&
                d3d11Context.find("m_Context->RSSetScissorRects(1, &scissor)") != std::string::npos,
            "Editor multi-viewport ownership or D3D11 main-rect restoration regressed"))
        return false;
    if (!Check(inspectorSections.find("CommitSceneNameEdit") != std::string::npos &&
                   inspectorSections.find("CommitSceneGravityEdit") != std::string::npos &&
                   inspectorSections.find("CommitSceneMainCameraHintEdit") != std::string::npos &&
                   inspectorSections.find("CommitSceneAmbientIntensityEdit") != std::string::npos &&
                   inspectorSections.find("Main Camera Hint") != std::string::npos &&
                   inspectorSections.find("Ambient Intensity") != std::string::npos &&
                   inspectorSections.find("Commands().SetSceneName") != std::string::npos &&
                   inspectorSections.find("Commands().SetSceneGravity") != std::string::npos &&
                   inspectorSections.find("Commands().SetSceneMainCameraHint") != std::string::npos &&
                   inspectorSections.find("Commands().SetSceneAmbientIntensity") != std::string::npos &&
                   inspectorSections.find("EditorCommandOperator commandOperator") != std::string::npos &&
                   inspectorSections.find("#include \"Editor/EditorCommand.h\"") == std::string::npos &&
                   inspectorSections.find("#include \"Editor/EditorUndoUtil.h\"") == std::string::npos &&
                   inspectorSections.find("LambdaEditorCommand") == std::string::npos &&
                   inspectorSections.find("scene->SetName(nameBuf.data())") == std::string::npos &&
                   inspectorSections.find("scene->SetMainCameraHintActorID") == std::string::npos &&
                   inspectorSections.find("scene->SetAmbientIntensity") == std::string::npos &&
                   inspectorSections.find("SetGravity(grav)") == std::string::npos &&
                   operatorsHeader.find("SetSceneName") != std::string::npos &&
                   operatorsHeader.find("SetSceneGravity") != std::string::npos &&
                   operatorsHeader.find("SetSceneMainCameraHint") != std::string::npos &&
                   operatorsHeader.find("SetSceneAmbientIntensity") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetSceneName") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetSceneGravity") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetSceneMainCameraHint") != std::string::npos &&
                   operatorsSource.find("EditorCommandOperator::SetSceneAmbientIntensity") != std::string::npos &&
                   sceneHeader.find("GetMainCameraHintActorID") != std::string::npos &&
                   sceneHeader.find("SetMainCameraHintActorID") != std::string::npos &&
                   sceneHeader.find("GetAmbientIntensity") != std::string::npos &&
                   sceneHeader.find("SetAmbientIntensity") != std::string::npos &&
                   sceneSerializer.find("\"mainCameraHintActorID\"") != std::string::npos &&
                   sceneSerializer.find("\"ambientIntensity\"") != std::string::npos &&
                   sceneLighting.find("out.ambientIntensity = scene.GetAmbientIntensity()") != std::string::npos &&
                   gameViewportSource.find("scene.GetMainCameraHintActorID()") != std::string::npos &&
                   gameViewportSource.find("hintedActor->GetComponent<CameraComponent>()") != std::string::npos,
               "Scene Settings still bypass EditorCommandOperator edit helpers"))
        return false;
    if (!Check(inspectorSections.find("DrawAssetMetadataHeader") != std::string::npos &&
                   inspectorSections.find("UUID: %s") != std::string::npos &&
                   inspectorSections.find("Import State: %s") != std::string::npos &&
                   inspectorSections.find("Artifact: %s") != std::string::npos &&
                   inspectorSections.find("Diagnostics") != std::string::npos &&
                   inspectorSections.find("operators->Assets().Reimport") != std::string::npos &&
                   inspectorSections.find("DrawImportSettingsEditor") != std::string::npos &&
                   inspectorSections.find("Settings JSON") != std::string::npos &&
                   inspectorSections.find("ParseImportSettingsJson") != std::string::npos &&
                   inspectorSections.find("Reset Settings") != std::string::npos &&
                   inspectorSections.find("DrawAssetDependencySection") != std::string::npos &&
                   inspectorSections.find("database.GetDependencies(info.uuid)") != std::string::npos &&
                   inspectorSections.find("database.GetReferencers(info.uuid)") != std::string::npos &&
                   inspectorSections.find("DrawAssetSceneReferenceList") != std::string::npos &&
                   inspectorSections.find("DrawAssetProjectSceneReferenceList") != std::string::npos &&
                   inspectorSections.find("operators->Assets().FindSceneReferences") != std::string::npos &&
                   inspectorSections.find("operators->Assets().FindProjectSceneReferences") != std::string::npos &&
                   inspectorSections.find("Scene References (") != std::string::npos &&
                   inspectorSections.find("Project Scene References (") != std::string::npos &&
                   inspectorSections.find("operators->Selection().SelectAsset(context, path)") != std::string::npos &&
                   inspectorSections.find("DrawAssetRecordList(context, \"Dependencies\"") != std::string::npos &&
                   inspectorSections.find("DrawAssetRecordList(context, \"Referencers\"") != std::string::npos &&
                   inspectorSections.find("DrawUnresolvedDependencyList") != std::string::npos &&
                   inspectorSections.find("Missing dependency UUID") != std::string::npos &&
                   inspectorSections.find("database.ValidateAgainstProject(projectRoot, validationReport)") !=
                       std::string::npos &&
                   inspectorSections.find("DrawAssetValidationIssueList") != std::string::npos &&
                   inspectorSections.find("Validation Issues") != std::string::npos &&
                   inspectorSections.find("ModelAssetInspectorSection") != std::string::npos &&
                   inspectorSections.find("PrefabAssetInspectorSection") != std::string::npos &&
                   inspectorSections.find("AudioAssetInspectorSection") != std::string::npos &&
                   inspectorSections.find("std::make_unique<ModelAssetInspectorSection>()") != std::string::npos &&
                   inspectorSections.find("std::make_unique<PrefabAssetInspectorSection>()") != std::string::npos &&
                   inspectorSections.find("std::make_unique<AudioAssetInspectorSection>()") != std::string::npos &&
                   inspectorSections.find("info->type != EditorAssetType::Model") != std::string::npos &&
                   inspectorSections.find("info->type != EditorAssetType::Prefab") != std::string::npos &&
                   inspectorSections.find("info->type != EditorAssetType::Audio") != std::string::npos &&
                   inspectorSections.find("Vertices: %u") != std::string::npos &&
                   inspectorSections.find("Animations: %zu") != std::string::npos &&
                   inspectorSections.find("Prefab UUID: %s") != std::string::npos &&
                   inspectorSections.find("Root Actor: %s") != std::string::npos &&
                   inspectorSections.find("Prefab Nodes") != std::string::npos &&
                   inspectorSections.find("Scene Instances: %zu") != std::string::npos &&
                   inspectorSections.find("Prefabs().SelectInstances") != std::string::npos &&
                   inspectorSections.find("Sample Rate: %u Hz") != std::string::npos &&
                   inspectorSections.find("Duration: %.3f seconds") != std::string::npos &&
                   inspectorSections.find("ModifyMaterialAssetField") != std::string::npos &&
                   inspectorSections.find("MaterialModifier modifier") != std::string::npos &&
                   inspectorSections.find("mat->SetBlendMode") == std::string::npos &&
                   inspectorSections.find("mat->SetAlphaThreshold") == std::string::npos &&
                   inspectorSections.find("mat->SetTwoSided") == std::string::npos &&
                   inspectorSections.find("mat->SetWireframe") == std::string::npos &&
                   inspectorSections.find("mat->SetParam") == std::string::npos &&
                   inspectorSections.find("tex->SetSampler") == std::string::npos &&
                   inspectorSections.find("SaveMaterialAssetToFile(*mat, path)") == std::string::npos,
               "asset inspector does not expose metadata/diagnostics through operator-backed UI"))
        return false;
    if (!Check(editorCommand.find("ModifyAssetCommand::Undo") != std::string::npos &&
                   editorCommand.find("RefreshModifiedAsset(context, m_AssetPath)") != std::string::npos &&
                   editorCommand.find("AssetManager::Get().Reload(path)") != std::string::npos,
               "asset modification commands do not refresh loaded assets on execute/undo"))
        return false;
    if (!Check(editorCommand.find("ModifyAssetsCommand::Execute") != std::string::npos &&
                   editorCommand.find("ModifyAssetsCommand::Undo") != std::string::npos &&
                   editorCommand.find("ModifyAssetsCommand::Apply") != std::string::npos,
               "batch asset modification command is missing execute/undo support"))
        return false;
    if (!Check(editorCommand.find("ShouldMarkSceneDirty") != std::string::npos &&
                   editorCommand.find("dynamic_cast<const IResourceCommand*>") != std::string::npos &&
                   compactEditorCommand.find("if(markSceneDirty)context.MarkSceneDirty()") != std::string::npos,
               "resource commands still mark the edited scene dirty"))
        return false;
    if (!Check(operatorsHeader.find("ReimportWithSettings") != std::string::npos &&
                   operatorsSource.find("EditorAssetOperator::ReimportWithSettings") != std::string::npos &&
                   operatorsSource.find("Update Import Settings") != std::string::npos &&
                   inspectorSections.find("operators->Assets().ReimportWithSettings") != std::string::npos &&
                   inspectorSections.find("EditorAssetOperator assetOperator") != std::string::npos &&
                   inspectorSections.find("assetOperator.ReimportWithSettings") != std::string::npos &&
                   inspectorSections.find("ImportSettingsWithTextureSampler") != std::string::npos &&
                   inspectorSections.find("importer->ReimportWithSettings") == std::string::npos,
               "asset import settings edits do not route through undoable operator command"))
        return false;
    if (!Check(editorLayer.find("\"edit.delete\"") != std::string::npos &&
                   editorLayer.find("\"edit.duplicate\"") != std::string::npos &&
                   editorLayer.find("\"edit.copy\"") != std::string::npos &&
                   editorLayer.find("\"asset.validate\"") != std::string::npos &&
                   editorLayer.find("ValidateAssets()") != std::string::npos &&
                   editorLayer.find("m_ImportService.RefreshValidation") != std::string::npos &&
                   editorLayer.find("m_ImportService.GetValidationSummaryText()") != std::string::npos &&
                   editorLayer.find("\"asset.open\"") != std::string::npos &&
                   editorLayer.find("\"asset.reveal\"") != std::string::npos &&
                   editorLayer.find("\"asset.createFolder\"") != std::string::npos &&
                   editorLayer.find("\"asset.createMaterial\"") != std::string::npos &&
                   editorLayer.find("\"asset.createTexture\"") != std::string::npos &&
                   editorLayer.find("\"asset.createPrefab\"") != std::string::npos &&
                   editorLayer.find("\"asset.createAngelScript\"") != std::string::npos &&
                   editorLayer.find("\"asset.createLua\"") != std::string::npos &&
                   editorLayer.find("\"asset.createShader\"") != std::string::npos &&
                   editorLayer.find("\"asset.createUI\"") != std::string::npos &&
                   editorLayer.find("\"asset.createScene\"") != std::string::npos &&
                   editorLayer.find("registerAssetTemplateAction") != std::string::npos &&
                   editorLayer.find("\"asset.move\"") != std::string::npos &&
                   editorLayer.find("\"asset.rename\"") != std::string::npos &&
                   editorLayer.find("ImGui::BeginMenu(\"Assets\")") != std::string::npos &&
                   editorLayer.find("\"File\", \"Project\", \"Assets\"") != std::string::npos &&
                   editorLayer.find("\"view.frameSelected\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.expandAll\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.collapseAll\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.createEmptyParent\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.unparent\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.moveUp\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.moveDown\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.selectChildren\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.selectSubtree\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.selectParent\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.selectPreviousSibling\"") != std::string::npos &&
                   editorLayer.find("\"hierarchy.selectNextSibling\"") != std::string::npos &&
                   editorLayer.find("CanFocusedPanelHandleAction") != std::string::npos &&
                   editorLayer.find("CanVisiblePanelHandleAction") != std::string::npos &&
                   editorLayer.find("CanVisiblePanelHandleAction(\"edit.rename\")") != std::string::npos &&
                   editorLayer.find("SetPanelFocusRequestHandler") != std::string::npos &&
                   editorLayer.find("panel->RequestFocus()") != std::string::npos &&
                   editorLayer.find("panel->CanHandleEditorAction") != std::string::npos &&
                   compactEditorLayer.find("panel&&panel->IsVisible()&&panel->HandleEditorAction") !=
                       std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.delete\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.duplicate\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.copy\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.paste\")") != std::string::npos &&
                   editorLayer.find("operators->Commands().HasActorClipboard()") != std::string::npos &&
                   editorLayer.find("operators->Commands().HasAssetClipboard()") != std::string::npos &&
                   editorLayer.find("DuplicateSelection(context)") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.rename\")") != std::string::npos &&
                   editorLayer.find("Rename was not handled") == std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"edit.selectAll\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.expandAll\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.collapseAll\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.createEmptyParent\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.unparent\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.moveUp\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.moveDown\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.selectChildren\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.selectSubtree\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.selectParent\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.selectPreviousSibling\")") != std::string::npos &&
                   editorLayer.find("DispatchPanelAction(\"hierarchy.selectNextSibling\")") != std::string::npos &&
                   editorLayer.find("scene->ForEach([&](Actor& actor)") == std::string::npos &&
                   editorLayer.find("Rename is handled by the focused panel inline editor") == std::string::npos &&
                   editorLayer.find("Frame Selected requested") == std::string::npos &&
                   editorLayer.find("operators->Viewport().FrameSelected") != std::string::npos &&
                   shortcutMap.find("edit.delete") != std::string::npos &&
                   shortcutMap.find("edit.duplicate") != std::string::npos,
               "global edit actions or shortcuts are not registered"))
        return false;
    if (!Check(operatorsHeader.find("FrameSelected") != std::string::npos &&
                   operatorsHeader.find("FrameTarget") != std::string::npos &&
                   operatorsHeader.find("DropModel") != std::string::npos &&
                   operatorsSource.find("EditorViewportOperator::FrameSelected") != std::string::npos &&
                   operatorsSource.find("EditorViewportOperator::FrameDirection") != std::string::npos &&
                   operatorsSource.find("EditorViewportOperator::DropModel") != std::string::npos &&
                   operatorsSource.find("MakeSceneSnapshotCommand(\"Drop Model\"") != std::string::npos,
               "viewport camera controls are not centralized in EditorViewportOperator"))
        return false;
    if (!Check(viewportPanel.find("viewport.FrameDirection") == std::string::npos &&
                   viewportPanel.find("viewport.OrbitAroundFocus") == std::string::npos &&
                   viewportPanel.find("viewport.ToggleProjectionMode") == std::string::npos &&
                   viewportPanel.find("operators->Viewport().FrameDirection") != std::string::npos &&
                   viewportPanel.find("operators->Viewport().OrbitAroundSelection") != std::string::npos &&
                   viewportPanel.find("operators->Viewport().DropModel") != std::string::npos &&
                   viewportPanel.find("scene->CreateActor") == std::string::npos &&
                   viewportPanel.find("AddComponent<MeshRendererComponent>") == std::string::npos &&
                   viewportPanel.find("MakeSceneSnapshotCommand(\"Drop Model\"") == std::string::npos,
               "ViewportPanel still mutates scene viewport camera or model-drop scene data directly"))
        return false;
    if (!Check(sceneViewportHeader.find("FrameTarget") != std::string::npos &&
                   sceneViewportSource.find("Editor/") == std::string::npos,
               "runtime SceneViewport controller is missing FrameTarget or depends on Editor"))
        return false;
    if (!Check(uiFacade.find("SetDefaultNamespace(\"Commands\")") != std::string::npos &&
                   uiFacade.find("operators->Commands().CreateActor") != std::string::npos &&
                   uiFacade.find("operators->Commands().SetActorTag") != std::string::npos &&
                   uiFacade.find("operators->Commands().SetActorLayer") != std::string::npos &&
                   uiFacade.find("context->GetSelection().SelectActorID(actorID)") == std::string::npos,
               "AngelScript facade does not route command/selection bindings through operators")) {
        return false;
    }
    return true;
}

MYENGINE_REGISTER_TEST("Editor", "TestEditorUIFacadeDoesNotExposeRawImGuiContract",
                       TestEditorUIFacadeDoesNotExposeRawImGuiContract);
MYENGINE_REGISTER_TEST("Editor", "TestEditorLayerKeepsCppPanelsWithScriptSidecar",
                       TestEditorLayerKeepsCppPanelsWithScriptSidecar);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPlatformViewportDpiContract", TestEditorPlatformViewportDpiContract);
MYENGINE_REGISTER_TEST("Editor", "TestEditorOperatorSourceContracts", TestEditorOperatorSourceContracts);

} // namespace
