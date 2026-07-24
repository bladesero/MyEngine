package("angelscript")
    set_homepage("https://www.angelcode.com/angelscript/")
    set_description("AngelScript - The AngelCode Scripting Library")
    set_license("zlib")

    add_urls("https://www.angelcode.com/angelscript/sdk/files/angelscript_$(version).zip")
    add_versions("2.38.0", "b33b5dbcda10317ef67d628353d83246984ce6fcac102d4dc2aed121eba52e6f")

    on_install("windows", "macosx", "linux", function (package)
        -- The zip extracts into the CWD with structure:
        --   angelscript/include/angelscript.h
        --   angelscript/source/*.cpp
        -- Write a minimal xmake.lua at the extraction root to build the library.
        --
        -- We must NOT use a separate sub-directory for xmake.lua here because
        -- package.tools.xmake.install() needs it at the root of the source dir.

        local kind = package:config("shared") and "shared" or "static"

        -- Collect all .cpp files from angelscript/source/ except platform-specific callfuncs
        local src_files = os.files("angelscript/source/as_*.cpp")
        -- Remove platform-specific callfunc files for non-relevant architectures
        -- (keep only the ones needed for the current arch; xmake will handle the rest)
        -- We just add all .cpp files and let the linker drop unused ones.

        local asm_file = ""
        if is_plat("macosx") then
            if is_arch("arm64") then
                asm_file = [[
                add_files("angelscript/source/as_callfunc_arm64_xcode.S")
                ]]
            else
                asm_file = [[
                add_files("angelscript/source/as_callfunc_x64_gcc.cpp")
                ]]
            end
        elseif is_plat("linux") then
            if is_arch("arm64", "aarch64") then
                asm_file = [[
                add_files("angelscript/source/as_callfunc_arm64_gcc.S")
                ]]
            else
                asm_file = [[
                add_files("angelscript/source/as_callfunc_x64_gcc.cpp")
                ]]
            end
        elseif is_plat("windows") then
            if is_arch("arm64") then
                asm_file = [[
                add_files("angelscript/source/as_callfunc_arm64_msvc.asm")
                ]]
            else
                asm_file = [[
                add_files("angelscript/source/as_callfunc_x64_msvc.asm")
                ]]
            end
        end

        local content = string.format([[
            add_rules("mode.debug", "mode.release")
            set_languages("c++17")
            target("angelscript")
                set_kind("%s")
                add_includedirs("angelscript/include", {public = true})
                add_headerfiles("angelscript/include/(**.h)")
                add_files("angelscript/source/as_*.cpp")
%s
                if not is_plat("windows") then
                    add_syslinks("pthread")
                end
                if is_plat("macosx") then
                    add_frameworks("CoreFoundation")
                end
        ]], kind, asm_file)

        io.writefile("xmake.lua", content)

        import("package.tools.xmake").install(package)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <angelscript.h>
            void test() {
                asIScriptEngine* engine = asCreateScriptEngine();
                if (engine) engine->ShutDownAndRelease();
            }
        ]]}, {configs = {languages = "c++17"}}))
    end)
