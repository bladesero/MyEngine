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
