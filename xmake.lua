set_project("MyEngine")
set_version("0.1.0")
set_xmakever("2.8.0")

add_repositories("myengine-packages .", {rootdir = os.scriptdir()})

add_rules("mode.debug", "mode.release")
set_languages("c++17")
set_warnings("all")

-- ---------------------------------------------------------------------------
-- Memory subsystem (ME_* heap helpers / future MemoryService)
-- Configure: xmake f --mem_stats=y|n --mem_tracking=y|n --mem_guard=y|n
-- Defaults: stats on; tracking+guard follow current mode (on in debug).
-- ---------------------------------------------------------------------------
option("mem_stats", function()
    set_default(true)
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_STATS: per-tag bytes / live counts / lifetime calls")
    add_defines("MYENGINE_MEM_STATS")
end)

option("mem_tracking", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_TRACKING: live allocation map + leak dump with file:line")
    add_defines("MYENGINE_MEM_TRACKING")
end)

option("mem_guard", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_GUARD: allocation poison fill + tail canaries")
    add_defines("MYENGINE_MEM_GUARD")
end)

add_requires("libsdl3 3.2.14", { configs = { shared = true } })
-- imgui pulls libsdl3 as a transitive dep; without this, that instance defaults to static and you get
-- SDL3-static.lib + SDL3.lib together (LNK2005 duplicate symbols on MSVC).
add_requireconfs("**.libsdl3", { version = "3.2.14", configs = { shared = true }, override = true })
add_requires("nlohmann_json 3.11.3")
add_requires("stb")
add_requires("myengine-rmlui 6.2", { alias = "rmlui", configs = {
    freetype = true,
    lua = false,
    svg = false,
    lottie = false,
    shared = false
} })
add_requires("tinyobjloader")
add_requires("joltphysics v5.5.0", { configs = {
    shared = false,
    object_layer_bits = "32",
    object_stream = false,
    -- xmake-repo's Jolt 5.5.0 Windows binary exposes the debug-renderer ABI.
    -- Keep the ABI define consistent; MyEngine does not instantiate a debug renderer.
    debug_renderer = true,
    cross_platform_deterministic = false
} })
add_requires("angelscript 2.38.0")

function add_vulkan_loader_link()
    add_syslinks("vulkan-1")
    local sdk = os.getenv("VULKAN_SDK")
    if sdk and sdk ~= "" then
        local libdir = path.join(sdk, "Lib")
        if os.isdir(libdir) then
            add_linkdirs(libdir)
            return
        end
    end
    local roots = os.dirs("C:/VulkanSDK/*")
    if roots then
        table.sort(roots)
        local selected = roots[#roots]
        if selected then
            local libdir = path.join(selected, "Lib")
            if os.isdir(libdir) then
                add_linkdirs(libdir)
            end
        end
    end
end

-- Must live in rule after_build: root xmake.lua locals use project-scope `os` (no os.cp).
rule("copy_game_content")
    after_build(function (target)
        local destdir = target:targetdir()
        for _, name in ipairs({"Content", "EngineContent", "ProjectTemplates"}) do
            local src = path.absolute(path.join(os.projectdir(), name))
            if os.isdir(src) then
                local dest = path.join(destdir, name)
                if os.isdir(dest) then
                    os.rm(dest)
                end
                os.cp(src, destdir)
            end
        end
    end)
rule_end()

rule("copy_slang_tool")
    after_build(function (target)
        local exe = is_plat("windows") and "slangc.exe" or "slangc"
        local slangc = os.getenv("MYENGINE_SLANGC")
        if not slangc or slangc == "" then
            local pathenv = os.getenv("PATH") or ""
            local separator = is_plat("windows") and ";" or ":"
            for dir in pathenv:gmatch("[^" .. separator .. "]+") do
                local candidate = path.join(dir, exe)
                if os.isfile(candidate) then
                    slangc = candidate
                    break
                end
            end
        end
        if not slangc or slangc == "" or not os.isfile(slangc) then
            return
        end
        local destdir = target:targetdir()
        os.cp(slangc, destdir)
        local bindir = path.directory(slangc)
        local installdir = path.directory(bindir)
        if is_plat("windows") then
            for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                os.cp(dll, destdir)
            end
        elseif is_plat("macosx") then
            for _, dylib in ipairs(os.files(path.join(installdir, "lib", "*.dylib"))) do
                os.cp(dylib, destdir)
            end
            for _, dylib in ipairs(os.files(path.join(bindir, "*.dylib"))) do
                os.cp(dylib, destdir)
            end
        end
    end)
rule_end()

rule("copy_sdl_runtime")
    after_build(function (target)
        local pkg = target:pkg("libsdl3")
        if not pkg then
            return
        end
        local destdir = target:targetdir()
        if is_plat("windows") then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    os.cp(dll, destdir)
                end
            end
        elseif is_plat("macosx") then
            local libdir = path.join(pkg:installdir(), "lib")
            if os.isdir(libdir) then
                for _, dylib in ipairs(os.files(path.join(libdir, "libSDL3*.dylib"))) do
                    os.cp(dylib, destdir)
                end
            end
        end
    end)
rule_end()

rule("copy_runtime_library")
    after_build(function (target)
        local rt = target:dep("MyEngineRuntime")
        if not rt then
            return
        end
        local runtime = rt:targetfile()
        if not os.isfile(runtime) then
            return
        end
        local destdir = target:targetdir()
        local dest = path.join(destdir, path.filename(runtime))
        -- Same output dir as MyEngineRuntime: copying onto itself fails.
        if path.normalize(path.absolute(runtime)) ~= path.normalize(path.absolute(dest)) then
            os.cp(runtime, destdir)
        end
    end)
rule_end()

if is_plat("windows") then
    add_requires("vulkan-headers 1.4.335+0")
    add_requires("imgui", { version = "v1.91.3-docking", configs = { sdl3 = true, dx11 = true, dx12 = true, vulkan = true, vulkan_no_proto = true } })
else
    add_requires("imgui", { version = "v1.91.3-docking", configs = { sdl3 = true } })
end

if is_plat("macosx") then
    includes("xmake/imgui_metal.lua")
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
    add_rules("copy_sdl_runtime")
    add_files(
        "src/Runtime/RuntimeModule.cpp",
        "src/Runtime/Project/ProjectConfig.cpp",
        "src/Runtime/Project/ContentArchive.cpp",
        "src/Runtime/Project/CookManifest.cpp",
        "src/Runtime/Project/CookedProjectCache.cpp",
        "src/Runtime/Project/ContentPathPolicy.cpp",
        "src/Runtime/Project/RuntimeDependencies.cpp",
        "src/Runtime/Input/Input.cpp",
        "src/Runtime/Input/InputActionMap.cpp",
        "src/Runtime/Core/Event.cpp",
        "src/Runtime/Core/Logger.cpp",
        "src/Runtime/Core/Sha256.cpp",
        "src/Runtime/Core/CrashHandler.cpp",
        "src/Runtime/Core/Layer.cpp",
        "src/Runtime/Core/LayerStack.cpp",
        "src/Runtime/Core/Time.cpp",
        "src/Runtime/Core/Window.cpp",
        "src/Runtime/Core/Engine.cpp",
        "src/Runtime/Core/Application.cpp",
        "src/Runtime/Core/Memory/PlatformAlignedAlloc.cpp",
        "src/Runtime/Core/Memory/AllocTracker.cpp",
        "src/Runtime/Core/Memory/GeneralHeapAllocator.cpp",
        "src/Runtime/Core/Memory/LinearAllocator.cpp",
        "src/Runtime/Core/Memory/MemoryService.cpp",
        "src/Runtime/Audio/AudioClipAsset.cpp",
        "src/Runtime/Audio/AudioEngine.cpp",
        "src/Runtime/Audio/AudioSourceComponent.cpp",
        "src/Runtime/Assets/AssetManager.cpp",
        "src/Runtime/Assets/AssetDatabase.cpp",
        "src/Runtime/Assets/AssetImporter.cpp",
        "src/Runtime/Assets/AssetMeta.cpp",
        "src/Runtime/Assets/PrefabAsset.cpp",
        "src/Runtime/Assets/AssetImporters.cpp",
        "src/Runtime/Assets/GltfImporter.cpp",
        "src/Runtime/Assets/MaterialAsset.cpp",
        "src/Runtime/Assets/ScriptAsset.cpp",
        "src/Runtime/Assets/ShaderAsset.cpp",
        "src/Runtime/Assets/MeshAsset.cpp",
        "src/Runtime/Assets/TextureAsset.cpp",
        "thirdparty/angelscript_addons/scriptstdstring/scriptstdstring.cpp",
        "src/Runtime/Scripting/AngelScriptRuntime.cpp",
        "src/Runtime/Scripting/ScriptComponent.cpp",
        "src/Runtime/Physics/RigidBodyComponent.cpp",
        "src/Runtime/Physics/BoxColliderComponent.cpp",
        "src/Runtime/Physics/SphereColliderComponent.cpp",
        "src/Runtime/Physics/CapsuleColliderComponent.cpp",
        "src/Runtime/Physics/CharacterControllerComponent.cpp",
        "src/Runtime/Physics/CollisionShapes.cpp",
        "src/Runtime/Physics/PhysicsWorld.cpp",
        "src/Runtime/Animation/SkinnedMeshRendererComponent.cpp",
        "src/Runtime/Scene/Actor.cpp",
        "src/Runtime/Scene/ActorSubtreeSerializer.cpp",
        "src/Runtime/Scene/ComponentRegistry.cpp",
        "src/Runtime/Scene/MeshRendererComponent.cpp",
        "src/Runtime/Scene/PrefabSystem.cpp",
        "src/Runtime/Scene/Scene.cpp",
        "src/Runtime/Scene/SceneSerializer.cpp",
        "src/Runtime/Camera/Camera.cpp",
        "src/Runtime/Camera/CameraComponent.cpp",
        "src/Runtime/Renderer/Renderer.cpp",
        "src/Runtime/Renderer/RenderGraph.cpp",
        "src/Runtime/Renderer/RHI/ShaderReflection.cpp",
        "src/Runtime/Renderer/GpuUploadQueue.cpp",
        "src/Runtime/Renderer/LightComponent.cpp",
        "src/Runtime/Renderer/PostProcessComponent.cpp",
        "src/Runtime/Renderer/PostProcessPass.cpp",
        "src/Runtime/Renderer/ScreenUIPass.cpp",
        "src/Runtime/Renderer/EnvironmentPass.cpp",
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
        "src/Runtime/Game/SceneViewportController.cpp",
        "src/Runtime/Game/ViewportRenderExecution.cpp",
        "src/Runtime/Game/DefaultSceneFactory.cpp",
        "src/Runtime/Game/SceneRenderLayer.cpp",
        "src/Runtime/UI/Core/UICanvas.cpp",
        "src/Runtime/UI/Core/UIActorTreeBuilder.cpp",
        "src/Runtime/UI/Core/UICanvasComponent.cpp",
        "src/Runtime/UI/Core/UIComponents.cpp",
        "src/Runtime/UI/Core/UISystem.cpp",
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
            "src/Runtime/Renderer/D3D12Context.cpp",
            "src/Runtime/Renderer/VulkanContext.cpp"
        )
    elseif is_plat("macosx") then
        add_files("src/Runtime/Renderer/MetalContext.mm")
    end

    -- IDE / vsxmake: headers only appear in the project if listed (they are not compiled).
    add_headerfiles("src/Runtime/(**.h)", { public = true })

    if is_plat("windows") then
        add_rules("utils.symbols.export_all")
        add_syslinks("d3d11", "d3d12", "dxgi", "d3dcompiler", "comdlg32", "user32")
        add_vulkan_loader_link()
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
    add_packages("imgui", { public = true })

    add_options("mem_stats", "mem_tracking", "mem_guard")

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
        add_packages("vulkan-headers", { public = true })
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
    add_files("icon_tool_main.cpp")
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
    add_rules("copy_game_content", "copy_sdl_runtime", "copy_slang_tool", "copy_runtime_library")
    add_files("main.cpp")
    add_files(
        "src/Editor/EditorAssetRegistry.cpp",
        "src/Editor/EditorAction.cpp",
        "src/Editor/EditorCommand.cpp",
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
        "src/Editor/EditorPanel.cpp",
        "src/Editor/EditorProject.cpp",
        "src/Editor/EditorSelection.cpp",
        "src/Editor/EditorService.cpp",
        "src/Editor/EditorShaderWatchService.cpp",
        "src/Editor/EditorShortcutMap.cpp",
        "src/Editor/EditorUndoUtil.cpp",
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
        "src/Editor/CookDependencyGraph.cpp",
        "src/Editor/InspectorSections.cpp",
        "src/Editor/Panels/ToolbarPanel.cpp",
        "src/Editor/Panels/SceneHierarchyPanel.cpp",
        "src/Editor/Panels/ViewportPanel.cpp",
        "src/Editor/Panels/InspectorPanel.cpp",
        "src/Editor/Panels/AssetBrowserPanel.cpp",
        "src/Editor/Panels/LogPanel.cpp",
        "src/Editor/EditorImGuiBackend.cpp",
        "src/Editor/EditorImGuiVulkanBridge.cpp",
        "src/Editor/EditorResourceOperator.cpp",
        "thirdparty/ImGuizmo/ImGuizmo.cpp", { warnings = "none" }
    )
    add_includedirs("src", "src/Editor", "thirdparty/ImGuizmo")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
    add_deps("Lua")
    add_packages("tinyobjloader")
    add_packages("libsdl3")
    add_packages("nlohmann_json")
    add_packages("stb")
    add_packages("rmlui")
    add_packages("joltphysics")
    add_packages("angelscript")
    add_packages("imgui")
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
        add_vulkan_loader_link()
        add_packages("vulkan-headers")
    elseif is_plat("macosx") then
        add_files("src/Editor/EditorImGuiMetalBridge.mm")
        add_deps("imgui_metal")
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEnginePlayer")
    set_kind("binary")
    add_rules("copy_game_content", "copy_sdl_runtime", "copy_runtime_library")
    add_files("player_main.cpp")
    add_includedirs("src")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
    add_packages("libsdl3")
    add_defines("MYENGINE_ENABLE_IMGUI")
    if is_plat("windows") then
        add_files("src/Runtime/Miscs/Resources/MyEnginePlayer.rc")
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineCooker")
    set_kind("binary")
    add_rules("copy_slang_tool", "copy_runtime_library")
    add_files(
        "cooker_main.cpp",
        "src/Editor/ProjectPublisher.cpp",
        "src/Editor/CookDependencyGraph.cpp"
    )
    add_includedirs("src", "src/Editor")
    add_deps("MyEngineRuntime")
    add_deps("MyEngineIconTool")
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
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineEditorPackager")
    set_kind("binary")
    add_files("editor_packager_main.cpp")
    add_includedirs("src")
    add_deps("MyEngineRuntime")
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
target_end()

target("MyEngineTests")
    set_kind("binary")
    add_rules("copy_game_content", "copy_sdl_runtime", "copy_slang_tool", "copy_runtime_library")
    add_files(
        "tests/AssetsTests.cpp",
        "tests/EditorTests.cpp",
        "tests/EngineTests.cpp",
        "tests/PhysicsTests.cpp",
        "tests/ProjectTests.cpp",
        "tests/RendererTests.cpp",
        "tests/TestHarness.cpp",
        "tests/TestMain.cpp",
        "src/Editor/EditorAssetRegistry.cpp",
        "src/Editor/EditorAction.cpp",
        "src/Editor/EditorCommand.cpp",
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
        "src/Editor/EditorPanel.cpp",
        "src/Editor/EditorProject.cpp",
        "src/Editor/EditorSelection.cpp",
        "src/Editor/EditorService.cpp",
        "src/Editor/EditorShaderWatchService.cpp",
        "src/Editor/EditorShortcutMap.cpp",
        "src/Editor/EditorUndoUtil.cpp",
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
        "src/Editor/CookDependencyGraph.cpp",
        "src/Editor/InspectorSections.cpp",
        "src/Editor/Panels/ToolbarPanel.cpp",
        "src/Editor/Panels/SceneHierarchyPanel.cpp",
        "src/Editor/Panels/ViewportPanel.cpp",
        "src/Editor/Panels/InspectorPanel.cpp",
        "src/Editor/Panels/AssetBrowserPanel.cpp",
        "src/Editor/Panels/LogPanel.cpp",
        "src/Editor/EditorImGuiBackend.cpp",
        "src/Editor/EditorImGuiVulkanBridge.cpp",
        "src/Editor/EditorResourceOperator.cpp",
        "thirdparty/ImGuizmo/ImGuizmo.cpp", { warnings = "none" }
    )
    add_includedirs("src", "src/Runtime", "src/Editor", "thirdparty/ImGuizmo")
    add_deps("MyEngineRuntime")
    add_deps("Lua")
    add_options("mem_stats", "mem_tracking", "mem_guard")
    add_packages("tinyobjloader")
    add_packages("libsdl3")
    add_packages("nlohmann_json")
    add_packages("stb")
    add_packages("rmlui")
    add_packages("joltphysics")
    add_packages("angelscript")
    add_packages("imgui")
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
        add_vulkan_loader_link()
        add_packages("vulkan-headers")
    elseif is_plat("macosx") then
        add_files("src/Editor/EditorImGuiMetalBridge.mm")
        add_deps("imgui_metal")
    end
target_end()
