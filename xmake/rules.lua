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

rule("myengine.app_icons")
    before_build(function(target)
        local icon_tool = target:dep("MyEngineIconTool")
        if not icon_tool then
            raise(target:name() .. " uses myengine.app_icons without MyEngineIconTool")
        end
        local app_icons = import("scripts.app_icons",
                                 {rootdir = path.join(os.projectdir(), "xmake")})
        app_icons.generate(icon_tool:targetfile())
    end)
rule_end()

-- Must live in rule after_build: root xmake.lua locals use project-scope `os` (no os.cp).
rule("copy_game_content")
    after_build(function (target)
        local incremental_files = import("scripts.incremental_files",
                                         {rootdir = path.join(os.projectdir(), "xmake")})
        local destdir = target:targetdir()
        local roots = {}
        for _, name in ipairs({"Content", "EngineContent", "ProjectTemplates"}) do
            local src = path.absolute(path.join(os.projectdir(), name))
            if os.isdir(src) then
                table.insert(roots, {source = src, destination = name})
            end
        end
        local manifest = path.join(destdir, ".myengine", "content.manifest")
        incremental_files.sync_managed_directories(roots, destdir, manifest)
    end)
rule_end()

rule("copy_slang_tool")
    after_build(function (target)
        local incremental_files = import("scripts.incremental_files",
                                         {rootdir = path.join(os.projectdir(), "xmake")})
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
        incremental_files.copy_if_changed(slangc, path.join(destdir, path.filename(slangc)))
        local bindir = path.directory(slangc)
        local installdir = path.directory(bindir)
        if is_plat("windows") then
            for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                incremental_files.copy_if_changed(dll, path.join(destdir, path.filename(dll)))
            end
        elseif is_plat("macosx") then
            for _, dylib in ipairs(os.files(path.join(installdir, "lib", "*.dylib"))) do
                incremental_files.copy_if_changed(dylib,
                                                  path.join(destdir, path.filename(dylib)))
            end
            for _, dylib in ipairs(os.files(path.join(bindir, "*.dylib"))) do
                incremental_files.copy_if_changed(dylib,
                                                  path.join(destdir, path.filename(dylib)))
            end
        end
    end)
rule_end()

rule("copy_sdl_runtime")
    after_build(function (target)
        local incremental_files = import("scripts.incremental_files",
                                         {rootdir = path.join(os.projectdir(), "xmake")})
        local pkg = target:pkg("libsdl3")
        if not pkg then
            return
        end
        local destdir = target:targetdir()
        if is_plat("windows") then
            local bindir = path.join(pkg:installdir(), "bin")
            if os.isdir(bindir) then
                for _, dll in ipairs(os.files(path.join(bindir, "*.dll"))) do
                    incremental_files.copy_if_changed(
                        dll, path.join(destdir, path.filename(dll)))
                end
            end
        elseif is_plat("macosx") then
            local libdir = path.join(pkg:installdir(), "lib")
            if os.isdir(libdir) then
                for _, dylib in ipairs(os.files(path.join(libdir, "libSDL3*.dylib"))) do
                    incremental_files.copy_if_changed(
                        dylib, path.join(destdir, path.filename(dylib)))
                end
            end
        end
    end)
rule_end()

rule("copy_runtime_library")
    after_build(function (target)
        local incremental_files = import("scripts.incremental_files",
                                         {rootdir = path.join(os.projectdir(), "xmake")})
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
            incremental_files.copy_if_changed(runtime, dest)
        end
    end)
rule_end()
