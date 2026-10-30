#include "TestHarness.h"

#include "Editor/EditorDialogService.h"
#include "Editor/EditorContextMenu.h"
#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <imgui_internal.h>
#endif
#include "Editor/EditorImportService.h"
#include "Editor/AssetImportService.h"
#include "Project/PublishTargets.h"
#include "Editor/Cooking/ProjectValidator.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorOperators.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Scene/Actor.h"
#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string ReadEditorSource(const char* relative) {
    auto root = std::filesystem::current_path();
    while (!root.empty()) {
        const auto candidate = root / relative;
        std::ifstream input(candidate);
        if (input)
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto parent = root.parent_path();
        if (parent == root)
            break;
        root = parent;
    }
    return {};
}

bool ContainsIgnoringWhitespace(std::string source, std::string expected) {
    const auto strip = [](std::string& text) {
        text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }),
                   text.end());
    };
    strip(source);
    strip(expected);
    return source.find(expected) != std::string::npos;
}

bool TestEditorDialogCancellationAndFailureAreDistinct() {
    EditorDialogService service;
    EditorDialogResult result;
    const char* cancelled[] = {nullptr};
    service.Complete(EditorFileOperation::OpenScene, cancelled);
    if (!Check(service.ConsumeResult(result) && result.path.empty() && result.error.empty(),
               "cancelled dialog must not report an error"))
        return false;
    SDL_SetError("Acceptance portal unavailable");
    service.Complete(EditorFileOperation::OpenScene, nullptr);
    if (!Check(service.ConsumeResult(result) && result.path.empty() && result.error == "Acceptance portal unavailable",
               "failed dialog must retain the SDL error separately from cancellation"))
        return false;
    const char* selected[] = {"/tmp/scene with spaces.scene.json", nullptr};
    service.Complete(EditorFileOperation::SaveScene, selected);
    if (!Check(service.ConsumeResult(result) && result.path == selected[0] && result.error.empty(),
               "successful dialog must clear an earlier error and preserve the selected path"))
        return false;
    SDL_ClearError();
    return Check(!service.ConsumeResult(result), "dialog results must be consumed once");
}

bool TestEditorNestedMenuSourceContract() {
    const auto source = ReadEditorSource("src/Editor/EditorLayer.cpp");
    return Check(!source.empty() && ContainsIgnoringWhitespace(source, "DrawScriptMenuItems(prefix.c_str())") &&
                     ContainsIgnoringWhitespace(source, "DrawScriptMenuItems(\"Assets/Create\")") &&
                     ContainsIgnoringWhitespace(source, "DrawScriptMenuItems(\"Build\")") &&
                     ContainsIgnoringWhitespace(source, "DrawScriptMenuItems(\"Build/Debug\")") &&
                     ContainsIgnoringWhitespace(source, "item.path == \"File/New\" && item.target == \"scene.new\"") &&
                     ContainsIgnoringWhitespace(source, "\"Edit\", \"View\", \"Build\""),
                 "nested extensions must recurse, merge native submenus, and avoid duplicate core menus");
}

bool TestEditorDirtySceneProtectionSourceContract() {
    const auto source = ReadEditorSource("src/Editor/EditorLayer.cpp");
    const auto begin = source.find("void EditorLayer::ProcessDialogResults()");
    const auto end = source.find("void EditorLayer::SaveScene()", begin);
    const auto operations = begin != std::string::npos ? source.substr(begin, end - begin) : std::string{};
    return Check(ContainsIgnoringWhitespace(operations, "OpenScene && m_SceneLayer->IsDirty()") &&
                     ContainsIgnoringWhitespace(operations, "Save the current scene before creating a new scene.") &&
                     ContainsIgnoringWhitespace(operations, "Save the current scene before opening another scene.") &&
                     ContainsIgnoringWhitespace(operations, "!m_Context.IsEditing()"),
                 "new/open and delayed dialog completion must protect unsaved scenes and Play mode");
}

bool TestEditorCloseAndDialogLifetimeSourceContract() {
    const auto layer = ReadEditorSource("src/Editor/EditorLayer.cpp");
    const auto dialog = ReadEditorSource("src/Editor/EditorDialogService.cpp");
    return Check(ContainsIgnoringWhitespace(layer, "event.type != EventType::Quit || m_AllowQuit") &&
                     ContainsIgnoringWhitespace(layer, "m_CloseDialogRequested = true") &&
                     ContainsIgnoringWhitespace(layer, "Save and Close") &&
                     ContainsIgnoringWhitespace(layer, "Discard and Close") &&
                     ContainsIgnoringWhitespace(layer, "Final snapshot failed") &&
                     ContainsIgnoringWhitespace(dialog, "[state = m_State, operation]") &&
                     dialog.find("pair->first->Complete") == std::string::npos,
                 "dirty close requires confirmation and callbacks must retain state rather than service pointers");
}

bool TestEditorTextureTemplateDecodesAndSupportsUndo() {
    const auto root = std::filesystem::temp_directory_path() / "MyEngineTextureTemplateDecodeTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Content", error);
    EditorContext context;
    EditorCommandStack commands;
    EditorOperators operators;
    context.SetProjectRoot(root);
    context.SetCommandStack(&commands);
    const bool created = operators.Assets().CreateAssetFromTemplate(context, (root / "Content").string(), "texture");
    const auto path = root / "Content" / "NewTexture.tga";
    auto texture = created ? AssetManager::Get().Load<TextureAsset>(path.string()) : AssetHandle<TextureAsset>{};
    const bool decoded = texture && texture->GetWidth() == 2 && texture->GetHeight() == 2 &&
                         texture->GetPixelData().size() == 16 && texture->GetPixelData()[3] == 255;
    const bool undone = commands.Undo(context) && !std::filesystem::exists(path);
    const bool redone = commands.Redo(context) && std::filesystem::exists(path);
    std::filesystem::remove_all(root, error);
    return Check(created && decoded && undone && redone,
                 "default texture must decode as real RGBA pixels and preserve create/undo/redo");
}

bool TestEditorScriptInspectorDoesNotRequestRuntimeLoader() {
    const auto source = ReadEditorSource("src/Editor/InspectorSectionsAssetScene.cpp");
    const auto begin = source.find("class GenericAssetInspectorSection");
    const auto generic = begin == std::string::npos ? std::string{} : source.substr(begin);
    return Check(ContainsIgnoringWhitespace(generic, "extension == \".lua\" || extension == \".as\"") &&
                     generic.find("Run Lua Script") != std::string::npos &&
                     generic.find("Load<Asset>") == std::string::npos,
                 "Lua/AngelScript source inspection must not repeatedly invoke an unavailable runtime loader");
}

bool TestEditorProjectValidationSelectsNativeBackend() {
#if defined(MYENGINE_PLATFORM_LINUX)
    const std::vector<ShaderBackend> expected{ShaderBackend::Vulkan};
    return Check(ProjectValidator::ShaderBackendsForValidation("vulkan") == expected &&
                     ProjectValidator::ShaderBackendsForValidation("d3d11") == expected,
                 "Linux graph validation must compile Vulkan, including portable projects last edited on Windows");
#elif defined(MYENGINE_PLATFORM_MACOS)
    return Check(ProjectValidator::ShaderBackendsForValidation("metal") ==
                     std::vector<ShaderBackend>{ShaderBackend::Metal},
                 "macOS graph validation must compile Metal");
#else
    return Check(
        ProjectValidator::ShaderBackendsForValidation("d3d11") == std::vector<ShaderBackend>{ShaderBackend::D3D11} &&
            ProjectValidator::ShaderBackendsForValidation("d3d12") == std::vector<ShaderBackend>{ShaderBackend::D3D12},
        "Windows graph validation must preserve the requested Direct3D backend");
#endif
}

bool TestEditorImportsUseNativeCacheNamespace() {
    const auto root = std::filesystem::temp_directory_path() / "MyEngineNativeImportCacheTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    const auto source = root / "external.png";
    std::ofstream(source, std::ios::binary) << "passthrough fixture";
    AssetImportService imports;
    std::string message;
    const bool opened = imports.OpenProject(root / "Project", &message);
    const auto imported = opened ? imports.Import(source, "{}", &message) : AssetImportReport{};
    const auto expected = root / "Project" / "Library" / PublishTargets::kDefaultCacheTargetId;
    const auto relative = std::filesystem::path(imported.record.artifactPath).lexically_relative(expected);
    const bool native = imported.succeeded && !relative.empty() && *relative.begin() != "..";
    const auto cached = imported.succeeded ? imports.Reimport(imported.record.uuid, &message) : AssetImportReport{};
    std::filesystem::remove_all(root, error);
    return Check(native && cached.succeeded && cached.cacheHit,
                 "native import target directory and cache key must agree: " + message);
}

bool TestEditorMultiSelectionDefersSnapshotMutations() {
    const auto source = ReadEditorSource("src/Editor/Panels/InspectorPanel.cpp");
    const auto begin = source.find("if (multiActorSelection)");
    const auto end = source.find("if (actor && actor->IsPrefabRoot())", begin);
    const auto branch = begin == std::string::npos ? std::string{} : source.substr(begin, end - begin);
    return Check(
        ContainsIgnoringWhitespace(branch, "std::vector<std::function<void()>> pendingEdits") &&
            ContainsIgnoringWhitespace(branch,
                                       "pendingEdits.emplace_back([=]() { operators->Components().AddComponents") &&
            ContainsIgnoringWhitespace(branch,
                                       "pendingEdits.emplace_back([=]() { operators->Components().RemoveComponents") &&
            ContainsIgnoringWhitespace(branch, "ImGui::EndDisabled(); for (auto& edit : pendingEdits) edit(); return;"),
        "multi-selection snapshot mutations must run after UI stops dereferencing old actors");
}

bool TestEditorQueuedMultiEditsResolveActorsAfterSnapshots() {
    Scene scene("QueuedMultiEditLifetime");
    EditorContext context(&scene);
    EditorCommandStack commands;
    EditorOperators operators;
    context.SetCommandStack(&commands);
    context.SetOperators(&operators);
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");
    if (!first || !second)
        return false;
    const std::vector<uint64_t> ids{first->GetID(), second->GetID()};
    bool succeeded = true;
    // Match the UI queue's capture contract: IDs and stable services only.
    // Each operation below recreates actors, invalidating any earlier Actor*.
    std::vector<std::function<void()>> edits;
    edits.emplace_back([&context, &operators, &succeeded, ids]() {
        succeeded = operators.Components().AddComponents(context, ids, "BoxCollider") && succeeded;
    });
    edits.emplace_back([&context, &operators, &succeeded, ids]() {
        succeeded = operators.Commands().SetActorsTag(context, ids, "after-rebuild") && succeeded;
    });
    edits.emplace_back([&context, &operators, &succeeded, ids]() {
        succeeded = operators.Components().RemoveComponents(context, ids, "BoxCollider") && succeeded;
    });
    for (auto& edit : edits)
        edit();
    for (uint64_t id : ids) {
        Actor* actor = scene.FindByID(id);
        succeeded = actor && actor->GetTag() == "after-rebuild" && !actor->HasComponentType("BoxCollider") && succeeded;
    }
    if (!commands.Undo(context))
        return false;
    for (uint64_t id : ids) {
        Actor* actor = scene.FindByID(id);
        succeeded = actor && actor->HasComponentType("BoxCollider") && succeeded;
    }
    return Check(succeeded, "queued multi-edits must resolve stable IDs after each scene snapshot and support undo");
}

bool TestEditorAssetContextMenusHaveDistinctRowIdentity() {
    const auto source = ReadEditorSource("src/Editor/Panels/AssetBrowserPanel.cpp");
    return Check(ContainsIgnoringWhitespace(source, "ImGui::PushID(assetPath.c_str())") &&
                     ContainsIgnoringWhitespace(
                         source, "if (emptyRightClick && !assetRightClick && !ImGui::IsAnyItemHovered())") &&
                     source.find("EditorContextMenu::DetectWindow(\"##AssetCtxMenu\")") == std::string::npos,
                 "asset row popup IDs must be unique and background menu must not steal row clicks");
}

bool TestEditorMenuActionsRetainPanelTarget() {
    const auto source = ReadEditorSource("src/Editor/EditorLayer.cpp");
    const auto dispatchStart = source.find("bool EditorLayer::DispatchPanelAction");
    const auto predicateStart = source.find("bool EditorLayer::CanFocusedPanelHandleAction", dispatchStart);
    const auto end = source.find("bool EditorLayer::CanVisiblePanelHandleAction", predicateStart);
    const auto dispatch = source.substr(dispatchStart, predicateStart - dispatchStart);
    const auto predicate = source.substr(predicateStart, end - predicateStart);
    return Check(ContainsIgnoringWhitespace(dispatch, "panel->GetID() == m_LastFocusedPanelID") &&
                     ContainsIgnoringWhitespace(predicate, "panel->GetID() == m_LastFocusedPanelID") &&
                     ContainsIgnoringWhitespace(source, "m_LastFocusedPanelID = panel->GetID()"),
                 "menu focus must not disable or retarget panel actions such as asset Paste and Select All");
}

bool TestEditorPasteDoesNotCrossPanelClipboardDomains() {
    const auto source = ReadEditorSource("src/Editor/EditorLayer.cpp");
    return Check(
        ContainsIgnoringWhitespace(
            source, "targetPanel->GetID() == \"assetBrowser\" || targetPanel->GetID() == \"sceneHierarchy\"") &&
            ContainsIgnoringWhitespace(source, "return targetPanel->CanHandleEditorAction(context, \"edit.paste\");") &&
            ContainsIgnoringWhitespace(source, "targetPanel->HandleEditorAction(context, \"edit.paste\"); return;"),
        "asset/hierarchy paste ownership must reject incompatible clipboards rather than use global fallback");
}

bool TestEditorImportRejectsMalformedTextureWithoutLosingSource() {
    const auto root = std::filesystem::temp_directory_path() / "MyEngineMalformedTextureImportTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Project" / "Content", error);
    const auto source = root / "broken.png";
    std::ofstream(source, std::ios::binary) << "not PNG data";
    EditorContext context;
    context.SetProjectRoot(root / "Project");
    EditorImportService imports;
    imports.OnAttach(context);
    const bool rejected = !imports.Import(source.string());
    AssetDatabase database;
    std::string message;
    const bool opened = database.Open(root / "Project" / ".myengine" / "AssetDatabase.json", &message);
    const auto records = database.GetAll();
    bool failedRecord = opened && records.size() == 1;
    if (failedRecord) {
        failedRecord = records[0].state == AssetImportState::Failed && !records[0].diagnostics.empty() &&
                       std::filesystem::is_regular_file(records[0].sourcePath) &&
                       std::filesystem::is_regular_file(source);
        failedRecord = !imports.Reimport(records[0].uuid) && failedRecord;
    }
    imports.OnDetach();
    std::filesystem::remove_all(root, error);
    return Check(rejected && failedRecord,
                 "malformed image import/reimport must fail visibly, mark Failed and retain original/copied source");
}

bool TestEditorReimportDoesNotTrustCachedReadyTexture() {
    const auto root = std::filesystem::temp_directory_path() / "MyEngineReimportTextureDecodeTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Project" / "Content", error);
    const auto source = root / "valid.tga";
    const unsigned char tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 0x28, 255, 255, 255, 255};
    std::ofstream(source, std::ios::binary).write(reinterpret_cast<const char*>(tga), sizeof(tga));
    EditorContext context;
    context.SetProjectRoot(root / "Project");
    EditorImportService imports;
    imports.OnAttach(context);
    const bool initiallyValid = imports.Import(source.string());
    AssetDatabase database;
    std::string message;
    database.Open(root / "Project" / ".myengine" / "AssetDatabase.json", &message);
    const auto records = database.GetAll();
    bool artifactRejected = false;
    bool sourceRejected = false;
    if (initiallyValid && records.size() == 1) {
        std::ofstream(records[0].artifactPath, std::ios::binary | std::ios::trunc) << "corrupt cached artifact";
        artifactRejected = !imports.Reimport(records[0].uuid);
        std::ofstream(records[0].sourcePath, std::ios::binary | std::ios::trunc) << "corrupt source";
        sourceRejected = !imports.Reimport(records[0].uuid);
    }
    imports.OnDetach();
    std::filesystem::remove_all(root, error);
    return Check(initiallyValid && artifactRejected && sourceRejected,
                 "reimport must decode fresh bytes after cached artifact or source becomes malformed");
}

bool TestEditorContextMenuDefersSceneMutationUntilPopupAndTreeEnd() {
#if defined(MYENGINE_ENABLE_IMGUI)
    Scene scene("DeferredHierarchyMenu");
    EditorContext context(&scene);
    EditorCommandStack commands;
    EditorOperators operators;
    context.SetCommandStack(&commands);
    context.SetOperators(&operators);
    const uint64_t canvasID = operators.Commands().CreateUIActor(context, "canvas");
    Actor* originalCanvas = scene.FindByID(canvasID);
    if (!originalCanvas)
        return false;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    std::vector<std::function<void()>> pending;
    ImVec2 itemCenter{};
    bool executed = false;
    bool scopesBalanced = true;
    uint64_t textID = 0;
    auto frame = [&](bool open) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("HierarchyTestHost");
        if (open)
            ImGui::OpenPopup("ContextTest");
        ImGui::SetNextWindowPos({100, 100});
        {
            EditorContextMenu menu("ContextTest", &pending);
            if (menu.IsOpen()) {
                menu.AddAction("UI/Text", [&context, &operators, &executed, &textID, canvasID]() {
                    textID = operators.Commands().CreateUIActor(context, "text", canvasID);
                    executed = true;
                });
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                itemCenter = {(min.x + max.x) / 2, (min.y + max.y) / 2};
                scopesBalanced = scopesBalanced && (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup);
                menu.AddAction("Unused", [] {});
            }
        }
        scopesBalanced = scopesBalanced && !(ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup);
        ImGui::End();
        ImGui::Render();
    };
    frame(true);
    frame(false);
    io.AddMousePosEvent(itemCenter.x, itemCenter.y);
    frame(false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(false);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame(false);
    const bool deferred = !executed && pending.size() == 1 && originalCanvas->GetName() == "UI Canvas";
    for (auto& action : pending)
        action();
    Actor* text = scene.FindByID(textID);
    const bool created = executed && text && text->GetParent() && text->GetParent()->GetID() == canvasID;
    const bool undoRedo = created && commands.Undo(context) && scene.FindByID(textID) == nullptr &&
                          commands.Redo(context) && scene.FindByID(textID) != nullptr;
    ImGui::DestroyContext(gui);
    ImGui::SetCurrentContext(previous);
    return Check(scopesBalanced && deferred && created && undoRedo,
                 "clicked hierarchy menu must balance popup and defer ID-based scene replacement until traversal ends");
#else
    return true;
#endif
}

MYENGINE_REGISTER_TEST("Editor", "TestEditorContextMenuDefersSceneMutationUntilPopupAndTreeEnd",
                       TestEditorContextMenuDefersSceneMutationUntilPopupAndTreeEnd);
MYENGINE_REGISTER_TEST("Editor", "TestEditorReimportDoesNotTrustCachedReadyTexture",
                       TestEditorReimportDoesNotTrustCachedReadyTexture);
MYENGINE_REGISTER_TEST("Editor", "TestEditorImportRejectsMalformedTextureWithoutLosingSource",
                       TestEditorImportRejectsMalformedTextureWithoutLosingSource);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPasteDoesNotCrossPanelClipboardDomains",
                       TestEditorPasteDoesNotCrossPanelClipboardDomains);
MYENGINE_REGISTER_TEST("Editor", "TestEditorMenuActionsRetainPanelTarget", TestEditorMenuActionsRetainPanelTarget);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAssetContextMenusHaveDistinctRowIdentity",
                       TestEditorAssetContextMenusHaveDistinctRowIdentity);
MYENGINE_REGISTER_TEST("Editor", "TestEditorQueuedMultiEditsResolveActorsAfterSnapshots",
                       TestEditorQueuedMultiEditsResolveActorsAfterSnapshots);
MYENGINE_REGISTER_TEST("Editor", "TestEditorMultiSelectionDefersSnapshotMutations",
                       TestEditorMultiSelectionDefersSnapshotMutations);
MYENGINE_REGISTER_TEST("Editor", "TestEditorImportsUseNativeCacheNamespace", TestEditorImportsUseNativeCacheNamespace);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProjectValidationSelectsNativeBackend",
                       TestEditorProjectValidationSelectsNativeBackend);
MYENGINE_REGISTER_TEST("Editor", "TestEditorTextureTemplateDecodesAndSupportsUndo",
                       TestEditorTextureTemplateDecodesAndSupportsUndo);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptInspectorDoesNotRequestRuntimeLoader",
                       TestEditorScriptInspectorDoesNotRequestRuntimeLoader);
MYENGINE_REGISTER_TEST("Editor", "TestEditorDialogCancellationAndFailureAreDistinct",
                       TestEditorDialogCancellationAndFailureAreDistinct);
MYENGINE_REGISTER_TEST("Editor", "TestEditorNestedMenuSourceContract", TestEditorNestedMenuSourceContract);
MYENGINE_REGISTER_TEST("Editor", "TestEditorDirtySceneProtectionSourceContract",
                       TestEditorDirtySceneProtectionSourceContract);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCloseAndDialogLifetimeSourceContract",
                       TestEditorCloseAndDialogLifetimeSourceContract);
} // namespace
