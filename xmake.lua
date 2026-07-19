set_project("MyEngine")
set_version("0.1.0")
set_xmakever("2.8.0")

add_repositories("myengine-packages .", {rootdir = os.scriptdir()})

add_rules("mode.debug", "mode.release")
set_languages("c++17")
set_warnings("all")
if is_mode("release") then
    -- Keep optimized binaries while emitting PDBs for build-id-addressed crash
    -- symbolication. Published packages exclude these files; CI archives them.
    set_symbols("debug")
    set_strip("none")
end

local myengine_git_commit = os.getenv("MYENGINE_GIT_COMMIT") or "unknown"
add_defines("MYENGINE_GIT_COMMIT=" .. myengine_git_commit)

includes("xmake/options.lua")
includes("xmake/packages.lua")
includes("xmake/rules.lua")

local editor_sources = {
    "src/Editor/EditorAssetRegistry.cpp",
    "src/Editor/EditorAction.cpp",
    "src/Editor/EditorCommand.cpp",
    "src/Editor/EditorClipboardService.cpp",
    "src/Editor/EditorContextMenu.cpp",
    "src/Editor/EditorDragDrop.cpp",
    "src/Editor/EditorContext.cpp",
    "src/Editor/EditorDialogService.cpp",
    "src/Editor/EditorImportService.cpp",
    "src/Editor/AssetImportService.cpp",
    "src/Editor/EditorLayer.cpp",
    "src/Editor/EditorLayout.cpp",
    "src/Editor/EditorLayoutManager.cpp",
    "src/Editor/EditorLogService.cpp",
    "src/Editor/EditorLuaScriptService.cpp",
    "src/Editor/EditorNavigationBakeService.cpp",
    "src/Editor/EditorLightingBakeService.cpp",
    "src/Editor/EditorOperators.cpp",
    "src/Editor/EditorOperatorsAssets.cpp",
    "src/Editor/EditorOperatorsCommands.cpp",
    "src/Editor/EditorOperatorsComponents.cpp",
    "src/Editor/EditorOperatorsDragDrop.cpp",
    "src/Editor/EditorOperatorsPrefabs.cpp",
    "src/Editor/EditorOperatorsSelection.cpp",
    "src/Editor/EditorOperatorsTransactions.cpp",
    "src/Editor/EditorOperatorsViewport.cpp",
    "src/Editor/EditorPanel.cpp",
    "src/Editor/EditorProfiler.cpp",
    "src/Editor/EditorProject.cpp",
    "src/Editor/EditorProjectSettingsController.cpp",
    "src/Editor/EditorRecoveryService.cpp",
    "src/Editor/EditorSelection.cpp",
    "src/Editor/EditorService.cpp",
    "src/Editor/EditorShaderWatchService.cpp",
    "src/Editor/EditorShortcutMap.cpp",
    "src/Editor/EditorUndoUtil.cpp",
    "src/Editor/EditorUI/EditorAngelScriptDomain.cpp",
    "src/Editor/EditorUI/EditorScriptConfig.cpp",
    "src/Editor/EditorUI/EditorScriptHotReloadService.cpp",
    "src/Editor/EditorUI/EditorScriptRegistry.cpp",
    "src/Editor/EditorUI/EditorUIFacade.cpp",
    "src/Editor/EditorViewportControllers.cpp",
    "src/Editor/EditorWorkspace.cpp",
    "src/Editor/UI/EditorFontManager.cpp",
    "src/Editor/UI/EditorNotifications.cpp",
    "src/Editor/UI/EditorPropertyGrid.cpp",
    "src/Editor/UI/EditorStatusBar.cpp",
    "src/Editor/UI/EditorTheme.cpp",
    "src/Editor/UI/EditorUIScaleManager.cpp",
    "src/Editor/UI/EditorWidgets.cpp",
    "src/Editor/ProjectPublisher.cpp",
    "src/Editor/ProjectValidator.cpp",
    "src/Editor/CookDependencyGraph.cpp",
    "src/Editor/InspectorSections.cpp",
    "src/Editor/InspectorSectionsAddComponent.cpp",
    "src/Editor/InspectorSectionsAssetScene.cpp",
    "src/Editor/InspectorSectionsGameplay.cpp",
    "src/Editor/InspectorSectionsPhysics.cpp",
    "src/Editor/InspectorSectionsScripting.cpp",
    "src/Editor/InspectorSectionsTransformRender.cpp",
    "src/Editor/InspectorSectionsUI.cpp",
    "src/Editor/Panels/ToolbarPanel.cpp",
    "src/Editor/Panels/SceneHierarchyPanel.cpp",
    "src/Editor/Panels/ViewportPanel.cpp",
    "src/Editor/Panels/InspectorPanel.cpp",
    "src/Editor/Panels/AssetBrowserPanel.cpp",
    "src/Editor/Panels/LogPanel.cpp",
    "src/Editor/Panels/ProfilerPanel.cpp",
    "src/Editor/Panels/ShaderGraphPanel.cpp",
    "src/Editor/Panels/ScriptedToolPanel.cpp",
    "src/Editor/EditorImGuiBackend.cpp",
    "src/Editor/EditorResourceOperator.cpp"
}

local function add_myengine_editor_sources()
    for _, source in ipairs(editor_sources) do
        add_files(source)
    end
    add_files("thirdparty/ImGuizmo/ImGuizmo.cpp", { warnings = "none" })
end

target("Lua")
    set_kind("static")
    set_languages("c11")
    add_files("thirdparty/lua/*.c")
    remove_files("thirdparty/lua/lua.c", "thirdparty/lua/luac.c")
    add_headerfiles("thirdparty/lua/*.h", { public = true })
    add_includedirs("thirdparty/lua", { public = true })
    if is_plat("linux") then
        add_defines("LUA_USE_LINUX")
        add_syslinks("dl", "m")
    elseif is_plat("macosx") then
        add_defines("LUA_USE_MACOSX")
    end
target_end()

target("MyEngineRuntime")
    set_kind("shared")
    set_basename("runtime")
    -- All runtime consumers currently share this target directory. Keep SDL
    -- staging single-owner to avoid parallel after_build copies racing on Windows.
    -- If targets gain separate output directories, replace this with an atomic,
    -- dependency-tracked staging rule rather than attaching the copy rule per target.
    add_rules("copy_sdl_runtime")
    add_files(
        "src/Runtime/RuntimeModule.cpp",
        "src/Runtime/Project/ProjectConfig.cpp",
        "src/Runtime/Project/JsonMigrationRegistry.cpp",
        "src/Runtime/Project/SaveGame.cpp",
        "src/Runtime/Project/ContentArchive.cpp",
        "src/Runtime/Project/CookManifest.cpp",
        "src/Runtime/Project/CookedProjectCache.cpp",
        "src/Runtime/Project/ContentPathPolicy.cpp",
        "src/Runtime/Project/RuntimeDependencies.cpp",
        "src/Runtime/Project/RuntimePerformanceProfile.cpp",
        "src/Runtime/Project/RuntimeUserSettings.cpp",
        "src/Runtime/Input/Input.cpp",
        "src/Runtime/Input/InputGlyphAtlas.cpp",
        "src/Runtime/Input/InputActionMap.cpp",
        "src/Runtime/Core/Event.cpp",
        "src/Runtime/Core/Logger.cpp",
        "src/Runtime/Core/RuntimeFileSystem.cpp",
        "src/Runtime/Core/Sha256.cpp",
        "src/Runtime/Core/CrashHandler.cpp",
        "src/Runtime/Core/TransactionalFileWriter.cpp",
        "src/Runtime/Core/Layer.cpp",
        "src/Runtime/Core/LayerStack.cpp",
        "src/Runtime/Core/Time.cpp",
        "src/Runtime/Core/Window.cpp",
        "src/Runtime/Core/Engine.cpp",
        "src/Runtime/Core/FrameStats.cpp",
        "src/Runtime/Core/Application.cpp",
        "src/Runtime/Core/Memory/PlatformAlignedAlloc.cpp",
        "src/Runtime/Core/Memory/AllocTracker.cpp",
        "src/Runtime/Core/Memory/GeneralHeapAllocator.cpp",
        "src/Runtime/Core/Memory/LinearAllocator.cpp",
        "src/Runtime/Core/Memory/MemoryService.cpp",
        "src/Runtime/Audio/AudioClipAsset.cpp",
        "src/Runtime/Audio/AudioEngine.cpp",
        "src/Runtime/Audio/AudioSourceComponent.cpp",
        "src/Runtime/Audio/AudioListenerComponent.cpp",
        "src/Runtime/Assets/AssetManager.cpp",
        "src/Runtime/Assets/AssetDatabase.cpp",
        "src/Runtime/Assets/AssetImporter.cpp",
        "src/Runtime/Assets/AssetMeta.cpp",
        "src/Runtime/Assets/PrefabAsset.cpp",
        "src/Runtime/Assets/AssetImporters.cpp",
        "src/Runtime/Assets/GltfImporter.cpp",
        "src/Runtime/Assets/MaterialAsset.cpp",
        "src/Runtime/Assets/ModelCacheAsset.cpp",
        "src/Runtime/Assets/ScriptAsset.cpp",
        "src/Runtime/Assets/NavMeshAsset.cpp",
        "src/Runtime/Assets/LightingProbeAsset.cpp",
        "src/Runtime/Assets/ParticleAsset.cpp",
        "src/Runtime/Assets/ShaderAsset.cpp",
        "src/Runtime/Assets/ShaderGraph.cpp",
        "src/Runtime/Assets/MeshAsset.cpp",
        "src/Runtime/Assets/TextureAsset.cpp",
        "thirdparty/angelscript_addons/scriptstdstring/scriptstdstring.cpp",
        "src/Runtime/Scripting/AngelScriptRuntime.cpp",
        "src/Runtime/Scripting/ScriptBindingContext.cpp",
        "src/Runtime/Scripting/ScriptProfiler.cpp",
        "src/Runtime/Core/RuntimePerformanceBudget.cpp",
        "src/Runtime/Core/RuntimeAccessibility.cpp",
        "src/Runtime/Core/RuntimeQualityDegradation.cpp",
        "src/Runtime/Core/TaskService.cpp",
        "src/Runtime/Scripting/ScriptComponent.cpp",
        "src/Runtime/Physics/RigidBodyComponent.cpp",
        "src/Runtime/Physics/BoxColliderComponent.cpp",
        "src/Runtime/Physics/SphereColliderComponent.cpp",
        "src/Runtime/Physics/CapsuleColliderComponent.cpp",
        "src/Runtime/Physics/CharacterControllerComponent.cpp",
        "src/Runtime/Physics/CollisionShapes.cpp",
        "src/Runtime/Physics/PhysicsWorld.cpp",
        "src/Runtime/Animation/AnimatorController.cpp",
        "src/Runtime/Animation/AnimatorComponent.cpp",
        "src/Runtime/Animation/SkinnedMeshRendererComponent.cpp",
        "src/Runtime/Scene/Actor.cpp",
        "src/Runtime/Scene/ActorSubtreeSerializer.cpp",
        "src/Runtime/Scene/ComponentRegistry.cpp",
        "src/Runtime/Scene/TypeRegistry.cpp",
        "src/Runtime/Scene/WorldFrameScheduler.cpp",
        "src/Runtime/Scene/WorldZoneStreamer.cpp",
        "src/Runtime/Scene/MeshRendererComponent.cpp",
        "src/Runtime/Scene/PrefabSystem.cpp",
        "src/Runtime/Scene/Scene.cpp",
        "src/Runtime/Scene/SceneSerializer.cpp",
        "src/Runtime/Camera/Camera.cpp",
        "src/Runtime/Camera/CameraComponent.cpp",
        "src/Runtime/Camera/ThirdPersonCameraComponent.cpp",
        "src/Runtime/Gameplay/GameplayComponents.cpp",
        "src/Runtime/Gameplay/EnemyAIComponent.cpp",
        "src/Runtime/Navigation/NavigationWorld.cpp",
        "src/Runtime/Navigation/NavAgentComponent.cpp",
        "src/Runtime/Renderer/Renderer.cpp",
        "src/Runtime/Renderer/RenderGraph.cpp",
        "src/Runtime/Renderer/RHIConformance.cpp",
        "src/Runtime/Renderer/RHI/ShaderReflection.cpp",
        "src/Runtime/Renderer/RHI/RHIResourceStats.cpp",
        "src/Runtime/Renderer/SceneLighting.cpp",
        "src/Runtime/Renderer/ProbeLightingSystem.cpp",
        "src/Runtime/Renderer/ProbeBakeRenderer.cpp",
        "src/Runtime/Renderer/SceneRenderCollector.cpp",
        "src/Runtime/Renderer/GpuSceneDatabase.cpp",
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "src/Runtime/Renderer/MaterialResourceCache.cpp",
        "src/Runtime/Renderer/MaterialSystem.cpp",
        "src/Runtime/Renderer/ForwardRenderPasses.cpp",
        "src/Runtime/Renderer/GpuUploadQueue.cpp",
        "src/Runtime/Renderer/LightComponent.cpp",
        "src/Runtime/Renderer/ProbeComponents.cpp",
        "src/Runtime/Renderer/PostProcessComponent.cpp",
        "src/Runtime/Renderer/ParticleSystemComponent.cpp",
        "src/Runtime/Renderer/PostProcessPass.cpp",
        "src/Runtime/Renderer/ScreenUIPass.cpp",
        "src/Runtime/Renderer/GBufferPass.cpp",
        "src/Runtime/Renderer/DeferredLightingPass.cpp",
        "src/Runtime/Renderer/EnvironmentPass.cpp",
        "src/Runtime/Renderer/RenderBackendRegistry.cpp",
        "src/Runtime/Renderer/ShaderCacheService.cpp",
        "src/Runtime/Renderer/ShaderCooker.cpp",
        "src/Runtime/Renderer/ShaderGraphCompiler.cpp",
        "src/Runtime/Renderer/ShaderManager.cpp",
        "src/Runtime/Renderer/ShaderCompilerSlang.cpp",
        "src/Runtime/Renderer/ShaderCompilerD3D11.cpp",
        "src/Runtime/Renderer/ShaderCompilerD3D12.cpp",
        "src/Runtime/Renderer/MainPass.cpp",
        "src/Runtime/Renderer/ShadowPass.cpp",
        "src/Runtime/Game/GameLayer.cpp",
        "src/Runtime/Game/RenderViewport.cpp",
        "src/Runtime/Game/GameViewport.cpp",
        "src/Runtime/Game/TriangleLayer.cpp",
        "src/Runtime/Game/SceneLayer.cpp",
        "src/Runtime/Game/SceneManager.cpp",
        "src/Runtime/Game/GameFlowController.cpp",
        "src/Runtime/Game/RuntimeResourceBudget.cpp",
        "src/Runtime/Game/SceneViewportController.cpp",
        "src/Runtime/Game/ViewportRenderExecution.cpp",
        "src/Runtime/Game/DefaultSceneFactory.cpp",
        "src/Runtime/Game/SceneRenderLayer.cpp",
        "src/Runtime/UI/Core/UICanvas.cpp",
        "src/Runtime/UI/Core/UIActorTreeBuilder.cpp",
        "src/Runtime/UI/Core/UICanvasComponent.cpp",
        "src/Runtime/UI/Core/UIComponents.cpp",
        "src/Runtime/UI/Core/UISystem.cpp",
        "src/Runtime/UI/Core/RuntimeUIScreenStack.cpp",
        "src/Runtime/UI/Core/RuntimeUIScreenConfig.cpp",
        "src/Runtime/UI/Core/SubtitleSystem.cpp",
        "src/Runtime/UI/Input/UIInputSystem.cpp",
        "src/Runtime/UI/Rml/RmlAssetLoader.cpp",
        "src/Runtime/UI/Rml/RmlContextManager.cpp",
        "src/Runtime/UI/Rml/RmlInputAdapter.cpp",
        "src/Runtime/UI/Rml/RmlRenderInterface.cpp",
        "src/Runtime/UI/UIEventBridge.cpp",
        "src/Runtime/Miscs/IconsManager.cpp",
        "src/Runtime/Math/Mat4Inverse.cpp"
    )
    if is_plat("windows") then
        add_files(
            "src/Runtime/Renderer/D3D11Context.cpp",
            "src/Runtime/Renderer/D3D12Context.cpp"
        )
        if myengine_enable_vulkan() then
            add_files("src/Runtime/Renderer/VulkanContext.cpp")
        end
    elseif is_plat("macosx") then
        add_files("src/Runtime/Renderer/MetalContext.mm")
    end

    -- IDE / vsxmake: headers only appear in the project if listed (they are not compiled).
    add_headerfiles("src/Runtime/(**.h)", { public = true })

    if is_plat("windows") then
        add_rules("utils.symbols.export_all")
        add_syslinks("d3d11", "d3d12", "dxgi", "d3dcompiler", "comdlg32", "user32")
        if myengine_enable_vulkan() then
            add_vulkan_loader_link()
        end
        add_cxflags("/utf-8", { toolset = "msvc" })
    elseif is_plat("macosx") then
        add_deps("imgui_metal")
        add_frameworks("Metal", "MetalKit", "QuartzCore", "AppKit")
    end

    add_includedirs("src/Runtime", { public = true })
    add_includedirs("src")
    add_includedirs("thirdparty/miniaudio")
    add_includedirs("thirdparty/angelscript_addons/scriptstdstring")
    add_packages("libsdl3", { public = true })
    add_packages("nlohmann_json", { public = true })
    add_packages("stb", { public = true })
    add_packages("rmlui", { public = true })
    add_packages("tinyobjloader")
    add_packages("joltphysics", { public = true })
    add_packages("angelscript", { public = true })
    add_includedirs("thirdparty")
    add_packages("imgui")

    add_options("mem_stats", "mem_tracking", "mem_guard", "vulkan")

    add_defines("MYENGINE_ENABLE_IMGUI")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    -- ImGui comes from the static imgui package; do not set IMGUI_API=dllexport here (that only affects
    -- our .cpp files, not imgui.lib). utils.symbols.export_all re-exports symbols needed by the editor exe.

    if is_plat("windows") then
        add_defines("MYENGINE_PLATFORM_WINDOWS", { public = true })
        if myengine_enable_vulkan() then
            add_defines("MYENGINE_ENABLE_VULKAN", { public = true })
            add_packages("vulkan-headers", { public = true })
        end
    elseif is_plat("macosx") then
        add_defines("MYENGINE_PLATFORM_MACOS", { public = true })
    elseif is_plat("linux") then
        add_defines("MYENGINE_PLATFORM_LINUX", { public = true })
    end

    if is_plat("windows") then
        add_defines("MYENGINE_COMPILER_MSVC", { public = true })
    elseif is_plat("macosx", "iphoneos") then
        add_defines("MYENGINE_COMPILER_CLANG", { public = true })
    else
        add_defines("MYENGINE_COMPILER_GCC", { public = true })
    end

target_end()

target("MyEngineIconTool")
    set_kind("binary")
    add_files("src/Apps/IconToolMain.cpp")
    add_includedirs("src", "src/Runtime")
    add_deps("MyEngineRuntime")
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
    after_build(function (target)
        if not is_plat("windows") then
            return
        end
        local outdir = path.join(os.projectdir(), "build", "generated", "icons")
        os.mkdir(outdir)
        local root = path.join(os.projectdir(), "EngineContent", "Editor", "Icons")
        os.execv(target:targetfile(), {"--icon-root", root, "--icon", "engine-editor", "--output", path.join(outdir, "editor.ico")})
        os.execv(target:targetfile(), {"--icon-root", root, "--icon", "engine-player", "--output", path.join(outdir, "player.ico")})
        os.execv(target:targetfile(), {"--icon-root", root, "--icon", "engine-cooker", "--output", path.join(outdir, "cooker.ico")})
    end)
target_end()

target("MyEngineEditor")
    set_kind("binary")
    add_rules("copy_game_content", "copy_slang_tool", "copy_runtime_library")
    add_files("src/Apps/EditorMain.cpp")
    add_myengine_editor_sources()
    add_includedirs("src", "src/Editor", "thirdparty/ImGuizmo",
        "thirdparty/angelscript_addons/scriptstdstring")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
    add_deps("Lua")
    add_options("vulkan")
    add_packages("tinyobjloader")
    add_packages("libsdl3")
    add_packages("nlohmann_json")
    add_packages("stb")
    add_packages("rmlui")
    add_packages("joltphysics")
    add_packages("angelscript")
    add_packages("imgui")
    add_packages("imnodes")
    add_defines("MYENGINE_ENABLE_IMGUI")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
        add_files("src/Runtime/Miscs/Resources/MyEngineEditor.rc")
        add_cxflags("/utf-8", { toolset = "msvc" })
        add_syslinks("dxgi")
        if myengine_enable_vulkan() then
            add_files("src/Editor/EditorImGuiVulkanBridge.cpp")
            add_defines("MYENGINE_ENABLE_VULKAN")
            add_vulkan_loader_link()
            add_packages("vulkan-headers")
        end
    elseif is_plat("macosx") then
        add_files("src/Editor/EditorImGuiMetalBridge.mm")
        add_deps("imgui_metal")
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEnginePlayer")
    set_kind("binary")
    add_rules("copy_game_content", "copy_runtime_library")
    add_files("src/Apps/PlayerMain.cpp")
    add_includedirs("src")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
    add_options("vulkan")
    add_packages("libsdl3")
    if is_plat("windows") then
        add_files("src/Runtime/Miscs/Resources/MyEnginePlayer.rc")
        add_cxflags("/utf-8", { toolset = "msvc" })
        if myengine_enable_vulkan() then
            add_defines("MYENGINE_ENABLE_VULKAN")
        end
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineCooker")
    set_kind("binary")
    add_rules("copy_slang_tool", "copy_runtime_library")
    add_files(
        "src/Apps/CookerMain.cpp",
        "src/Editor/ProjectPublisher.cpp",
        "src/Editor/ProjectValidator.cpp",
        "src/Editor/CookDependencyGraph.cpp"
    )
    add_includedirs("src", "src/Editor")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
    add_options("vulkan")
    add_packages("nlohmann_json")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
        add_files("src/Runtime/Miscs/Resources/MyEngineCooker.rc")
        add_cxflags("/utf-8", { toolset = "msvc" })
        if myengine_enable_vulkan() then
            add_defines("MYENGINE_ENABLE_VULKAN")
        end
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineEditorPackager")
    set_kind("binary")
    add_files("src/Apps/EditorPackagerMain.cpp")
    add_includedirs("src")
    add_deps("MyEngineRuntime")
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineTests")
    set_kind("binary")
    add_rules("copy_game_content", "copy_slang_tool", "copy_runtime_library")
    add_files(
        "tests/AssetsTests.cpp",
        "tests/EditorSourceContractTests.cpp",
        "tests/EditorTests.cpp",
        "tests/EngineTests.cpp",
        "tests/PhysicsTests.cpp",
        "tests/ProjectTests.cpp",
        "tests/RendererTests.cpp",
        "tests/ScriptingTests.cpp",
        "tests/TestHarness.cpp",
        "tests/TestMain.cpp"
    )
    add_myengine_editor_sources()
    add_includedirs("src", "src/Runtime", "src/Editor", "thirdparty/ImGuizmo",
        "thirdparty/angelscript_addons/scriptstdstring")
    add_deps("MyEngineRuntime")
    add_deps("Lua")
    add_options("mem_stats", "mem_tracking", "mem_guard", "vulkan")
    add_packages("tinyobjloader")
    add_packages("libsdl3")
    add_packages("nlohmann_json")
    add_packages("stb")
    add_packages("rmlui")
    add_packages("joltphysics")
    add_packages("angelscript")
    add_packages("imgui")
    add_packages("imnodes")
    add_defines("MYENGINE_ENABLE_IMGUI")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
        add_syslinks("dxgi")
        if myengine_enable_vulkan() then
            add_files("src/Editor/EditorImGuiVulkanBridge.cpp")
            add_defines("MYENGINE_ENABLE_VULKAN")
            add_vulkan_loader_link()
            add_packages("vulkan-headers")
        end
    elseif is_plat("macosx") then
        add_files("src/Editor/EditorImGuiMetalBridge.mm")
        add_deps("imgui_metal")
    end
    after_build(function (target)
        os.cp(path.join(os.projectdir(), "tests", "fixtures"),
              path.join(target:targetdir(), "tests", "fixtures"))
    end)
target_end()
