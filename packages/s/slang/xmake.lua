package("slang")
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Making it easier to work with shaders (prebuilt binary)")
    set_license("MIT")

    add_configs("shared", { description = "Build shared library", default = true, type = "boolean", readonly = true })
    add_configs("embed_stdlib_source", { description = "Embed stdlib source in the binary", default = true, type = "boolean" })
    add_configs("slangc", { description = "Enable standalone compiler target", default = true, type = "boolean" })
    add_configs("gfx", { description = "Enable gfx targets", default = false, type = "boolean" })
    add_configs("slangd", { description = "Enable language server target", default = false, type = "boolean" })
    add_configs("slangrt", { description = "Enable runtime target", default = false, type = "boolean" })

    if is_plat("windows") and is_arch("x64") then
        add_urls("https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-windows-x86_64.zip")
        add_versions("v2025.6.3", "3f96b69f775566a748099a4fefa4cfe6a42227192a57857034b8e47bc8919024")
    elseif is_plat("macosx") and is_arch("arm64") then
        add_urls("https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-macos-aarch64.zip")
        add_versions("v2025.6.3", "notyet")
    elseif is_plat("macosx") and is_arch("x86_64") then
        add_urls("https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-macos-x86_64.zip")
        add_versions("v2025.6.3", "notyet")
    elseif is_plat("linux") and is_arch("x86_64") then
        add_urls("https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-linux-x86_64-glibc-2.17.zip")
        add_versions("v2025.6.3", "notyet")
    end

    on_install("windows", "macosx", "linux", function (package)
        os.cp("include", package:installdir("include"))
        os.cp("lib", package:installdir("lib"))
        os.cp("bin", package:installdir("bin"))
        package:addenv("PATH", "bin")
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({ test = [[
            #include <slang-com-ptr.h>
            #include <slang.h>
            void test() {
                Slang::ComPtr<slang::IGlobalSession> global_session;
                slang::createGlobalSession(global_session.writeRef());
            }
        ]] }, {configs = {languages = "c++17"}}))
    end)