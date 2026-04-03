set_project("MyEngine")
set_version("0.1.0")
set_xmakever("2.8.0")

add_rules("mode.debug", "mode.release")
set_languages("c++17")
set_warnings("all")

add_requires("libsdl3 3.2.14", { configs = { shared = true } })
-- imgui pulls libsdl3 as a transitive dep; without this, that instance defaults to static and you get
-- SDL3-static.lib + SDL3.lib together (LNK2005 duplicate symbols on MSVC).
add_requireconfs("**.libsdl3", { version = "3.2.14", configs = { shared = true }, override = true })
add_requires("nlohmann_json 3.11.3")
add_requires("stb")
add_requires("tinyobjloader")

if is_plat("windows") then
    add_requires("imgui", { version = "v1.91.3", configs = { sdl3 = true, dx11 = true, dx12 = true } })
else
    add_requires("imgui", { version = "v1.91.3", configs = { sdl3 = true } })
end

if is_plat("macosx") then
    includes("xmake/imgui_metal.lua")
end

target("MyEngineRuntime")
    set_kind("shared")
    set_basename("runtime")
    add_files(
        "src/Runtime/RuntimeModule.cpp",
        "src/Runtime/Input/Input.cpp",
        "src/Runtime/Core/Event.cpp",
        "src/Runtime/Core/Layer.cpp",
        "src/Runtime/Core/LayerStack.cpp",
        "src/Runtime/Core/Time.cpp",
        "src/Runtime/Core/Window.cpp",
        "src/Runtime/Core/Engine.cpp",
        "src/Runtime/Core/Application.cpp",
        "src/Runtime/Assets/AssetManager.cpp",
        "src/Runtime/Assets/AssetImporters.cpp",
        "src/Runtime/Assets/MeshAsset.cpp",
        "src/Runtime/Scene/Actor.cpp",
        "src/Runtime/Scene/MeshRendererComponent.cpp",
        "src/Runtime/Scene/Scene.cpp",
        "src/Runtime/Scene/SceneSerializer.cpp",
        "src/Runtime/Camera/Camera.cpp",
        "src/Runtime/Renderer/Renderer.cpp",
        "src/Runtime/Renderer/MainPass.cpp",
        "src/Runtime/Renderer/ShadowPass.cpp",
        "src/Runtime/Game/GameLayer.cpp",
        "src/Runtime/Game/TriangleLayer.cpp",
        "src/Runtime/Game/SceneLayer.cpp",
        "src/Runtime/Game/SceneRenderLayer.cpp",
        "src/Editor/EditorLayer.cpp"
    )

    if is_plat("windows") then
        add_files("src/Runtime/Renderer/D3D11Context.cpp", "src/Runtime/Renderer/D3D12Context.cpp")
    elseif is_plat("macosx") then
        add_files("src/Runtime/Renderer/MetalContext.mm")
    end

    -- IDE / vsxmake: headers only appear in the project if listed (they are not compiled).
    add_headerfiles("src/Runtime/(**.h)", { public = true })
    add_headerfiles("src/Editor/(**.h)")

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
    add_includedirs("src/Editor")
    add_packages("libsdl3", { public = true })
    add_packages("nlohmann_json", { public = true })
    add_packages("stb", { public = true })
    add_packages("tinyobjloader")
    add_packages("imgui", { public = true })

    add_defines("MYENGINE_ENABLE_IMGUI")
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
    add_files("main.cpp")
    add_includedirs("src", "src/Editor")
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
                -- Same output dir as MyEngineRuntime: copying onto itself fails (often "busy" if loaded).
                if path.normalize(path.absolute(dll)) ~= path.normalize(path.absolute(dest)) then
                    os.cp(dll, destdir)
                end
            end
        end
    end)
target_end()

target("MyEngineTests")
    set_kind("binary")
    add_files("tests/EngineTests.cpp")
    add_includedirs("src/Runtime")
    add_deps("MyEngineRuntime")
    add_packages("libsdl3", "nlohmann_json")
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
