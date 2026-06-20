set_project("MyEngine")
set_version("0.1.0")
set_xmakever("2.8.0")

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
add_requires("tinyobjloader")

-- Must live in rule after_build: root xmake.lua locals use project-scope `os` (no os.cp).
rule("copy_game_content")
    after_build(function (target)
        local src = path.absolute(path.join(os.projectdir(), "Content"))
        if not os.isdir(src) then
            return
        end
        local destdir = target:targetdir()
        local dest = path.join(destdir, "Content")
        if os.isdir(dest) then
            os.rm(dest)
        end
        os.cp(src, destdir)
    end)
rule_end()

if is_plat("windows") then
    add_requires("imgui", { version = "v1.91.3", configs = { sdl3 = true, dx11 = true, dx12 = true } })
else
    add_requires("imgui", { version = "v1.91.3", configs = { sdl3 = true } })
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
    add_files(
        "src/Runtime/RuntimeModule.cpp",
        "src/Runtime/Project/ProjectConfig.cpp",
        "src/Runtime/Project/ContentArchive.cpp",
        "src/Runtime/Project/CookManifest.cpp",
        "src/Runtime/Project/CookedProjectCache.cpp",
        "src/Runtime/Project/ContentPathPolicy.cpp",
        "src/Runtime/Project/RuntimeDependencies.cpp",
        "src/Runtime/Input/Input.cpp",
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
        "src/Runtime/Assets/AssetManager.cpp",
        "src/Runtime/Assets/AssetMeta.cpp",
        "src/Runtime/Assets/PrefabAsset.cpp",
        "src/Runtime/Assets/AssetImporters.cpp",
        "src/Runtime/Assets/GltfImporter.cpp",
        "src/Runtime/Assets/MaterialAsset.cpp",
        "src/Runtime/Assets/ShaderAsset.cpp",
        "src/Runtime/Assets/MeshAsset.cpp",
        "src/Runtime/Assets/TextureAsset.cpp",
        "src/Runtime/Scripting/ScriptRuntime.cpp",
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
        "src/Runtime/Renderer/Renderer.cpp",
        "src/Runtime/Renderer/RenderGraph.cpp",
        "src/Runtime/Renderer/RHI/ShaderReflection.cpp",
        "src/Runtime/Renderer/GpuUploadQueue.cpp",
        "src/Runtime/Renderer/LightComponent.cpp",
        "src/Runtime/Renderer/PostProcessComponent.cpp",
        "src/Runtime/Renderer/PostProcessPass.cpp",
        "src/Runtime/Renderer/EnvironmentPass.cpp",
        "src/Runtime/Renderer/ShaderManager.cpp",
        "src/Runtime/Renderer/ShaderCompilerD3D11.cpp",
        "src/Runtime/Renderer/ShaderCompilerD3D12.cpp",
        "src/Runtime/Renderer/MainPass.cpp",
        "src/Runtime/Renderer/ShadowPass.cpp",
        "src/Runtime/Game/GameLayer.cpp",
        "src/Runtime/Game/TriangleLayer.cpp",
        "src/Runtime/Game/SceneLayer.cpp",
        "src/Runtime/Game/SceneRenderLayer.cpp",
        "src/Runtime/Math/Mat4Inverse.cpp"
    )

    if is_plat("windows") then
        add_files("src/Runtime/Renderer/D3D11Context.cpp", "src/Runtime/Renderer/D3D12Context.cpp")
    elseif is_plat("macosx") then
        add_files("src/Runtime/Renderer/MetalContext.mm")
    end

    -- IDE / vsxmake: headers only appear in the project if listed (they are not compiled).
    add_headerfiles("src/Runtime/(**.h)", { public = true })

    if is_plat("windows") then
        add_rules("utils.symbols.export_all")
        add_syslinks("d3d11", "d3d12", "dxgi", "d3dcompiler", "comdlg32")
        add_cxflags("/utf-8", { toolset = "msvc" })
    elseif is_plat("macosx") then
        add_deps("imgui_metal")
        add_frameworks("Metal", "MetalKit", "QuartzCore", "AppKit")
    end

    add_includedirs("src/Runtime", { public = true })
    add_includedirs("src")
    add_packages("libsdl3", { public = true })
    add_packages("nlohmann_json", { public = true })
    add_packages("stb", { public = true })
    add_packages("tinyobjloader")
    add_deps("Lua")
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

    after_build(function (target)
        if not is_plat("windows") then
            return
        end
        local pkg = target:pkg("libsdl3")
        if pkg then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    os.cp(dll, target:targetdir())
                end
            end
        end
    end)
target_end()

target("MyEngineEditor")
    set_kind("binary")
    add_rules("copy_game_content")
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
        "src/Editor/EditorLayer.cpp",
        "src/Editor/EditorLayout.cpp",
        "src/Editor/EditorLogService.cpp",
        "src/Editor/EditorPanel.cpp",
        "src/Editor/EditorProject.cpp",
        "src/Editor/EditorSelection.cpp",
        "src/Editor/EditorService.cpp",
        "src/Editor/EditorShaderWatchService.cpp",
        "src/Editor/EditorUndoUtil.cpp",
        "src/Editor/EditorViewportControllers.cpp",
        "src/Editor/EditorWorkspace.cpp",
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
        "thirdparty/ImGuizmo/ImGuizmo.cpp", { warnings = "none" }
    )
    add_includedirs("src", "src/Editor", "thirdparty/ImGuizmo")
    add_deps("MyEngineRuntime")
    add_packages("tinyobjloader")
    add_defines("MYENGINE_ENABLE_IMGUI")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
    after_build(function (target)
        if not is_plat("windows") then
            return
        end
        local pkg = target:pkg("libsdl3")
        if pkg then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    os.cp(dll, target:targetdir())
                end
            end
        end
        local rt = target:dep("MyEngineRuntime")
        if rt then
            local dll = rt:targetfile()
            if os.isfile(dll) then
                local destdir = target:targetdir()
                local dest = path.join(destdir, path.filename(dll))
                -- Same output dir as MyEngineRuntime: copying onto itself fails (often "busy" if loaded).
                if path.normalize(path.absolute(dll)) ~= path.normalize(path.absolute(dest)) then
                    os.cp(dll, destdir)
                end
            end
        end
    end)
target_end()

target("MyEnginePlayer")
    set_kind("binary")
    add_rules("copy_game_content")
    add_files("player_main.cpp")
    add_includedirs("src")
    add_deps("MyEngineRuntime")
    add_packages("libsdl3")
    add_defines("MYENGINE_ENABLE_IMGUI")
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    set_rundir("$(projectdir)")
    after_build(function (target)
        if not is_plat("windows") then
            return
        end
        local pkg = target:pkg("libsdl3")
        if pkg then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    os.cp(dll, target:targetdir())
                end
            end
        end
        local rt = target:dep("MyEngineRuntime")
        if rt then
            local dll = rt:targetfile()
            if os.isfile(dll) then
                local destdir = target:targetdir()
                local dest = path.join(destdir, path.filename(dll))
                if path.normalize(path.absolute(dll)) ~= path.normalize(path.absolute(dest)) then
                    os.cp(dll, destdir)
                end
            end
        end
    end)
target_end()

target("MyEngineCooker")
    set_kind("binary")
    add_files(
        "cooker_main.cpp",
        "src/Editor/ProjectPublisher.cpp",
        "src/Editor/CookDependencyGraph.cpp"
    )
    add_includedirs("src", "src/Editor")
    add_deps("MyEngineRuntime")
    add_packages("nlohmann_json")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
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
    add_rules("copy_game_content")
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
        "src/Editor/EditorLayer.cpp",
        "src/Editor/EditorLayout.cpp",
        "src/Editor/EditorLogService.cpp",
        "src/Editor/EditorPanel.cpp",
        "src/Editor/EditorProject.cpp",
        "src/Editor/EditorSelection.cpp",
        "src/Editor/EditorService.cpp",
        "src/Editor/EditorShaderWatchService.cpp",
        "src/Editor/EditorUndoUtil.cpp",
        "src/Editor/EditorViewportControllers.cpp",
        "src/Editor/EditorWorkspace.cpp",
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
        "thirdparty/ImGuizmo/ImGuizmo.cpp", { warnings = "none" }
    )
    add_includedirs("src", "src/Runtime", "src/Editor", "thirdparty/ImGuizmo")
    add_deps("MyEngineRuntime")
    add_options("mem_stats", "mem_tracking", "mem_guard")
    add_packages("tinyobjloader")
    add_defines("MYENGINE_ENABLE_IMGUI")
    add_defines("MYENGINE_BUILD_ID=dev_0_1_0")
    if is_mode("release") then
        add_defines("MYENGINE_BUILD_CONFIGURATION=release")
    else
        add_defines("MYENGINE_BUILD_CONFIGURATION=debug")
    end
    if is_plat("windows") then
        add_cxflags("/utf-8", { toolset = "msvc" })
    end
    after_build(function (target)
        if not is_plat("windows") then
            return
        end
        local pkg = target:pkg("libsdl3")
        if pkg then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    os.cp(dll, target:targetdir())
                end
            end
        end
        local rt = target:dep("MyEngineRuntime")
        if rt then
            local dll = rt:targetfile()
            if os.isfile(dll) then
                local destdir = target:targetdir()
                local dest = path.join(destdir, path.filename(dll))
                if path.normalize(path.absolute(dll)) ~= path.normalize(path.absolute(dest)) then
                    os.cp(dll, destdir)
                end
            end
        end
    end)
target_end()
