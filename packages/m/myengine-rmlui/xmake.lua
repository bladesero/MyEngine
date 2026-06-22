package("myengine-rmlui")
    set_homepage("https://mikke89.github.io/RmlUiDoc/")
    set_description("RmlUi Core built by MyEngine's xmake-only package recipe.")
    set_license("MIT")

    add_urls("https://github.com/mikke89/RmlUi/archive/refs/tags/$(version).tar.gz",
             "https://github.com/mikke89/RmlUi.git")
    add_versions("6.2", "814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b")

    add_configs("freetype", {description = "Build with the default FreeType font engine.", default = true, type = "boolean"})
    add_configs("lua",      {description = "Build Lua bindings.", default = false, type = "boolean"})
    add_configs("rtti",     {description = "Build with RTTI and exceptions enabled.", default = true, type = "boolean"})
    add_configs("svg",      {description = "Build with SVG plugin enabled.", default = false, type = "boolean"})
    add_configs("lottie",   {description = "Build with Lottie plugin enabled.", default = false, type = "boolean"})

    if is_plat("windows") then
        add_syslinks("shlwapi", "imm32", "user32")
    elseif is_plat("macosx") then
        add_frameworks("Cocoa")
    end

    on_load(function (package)
        assert(not package:config("lua"), "MyEngine's RmlUi package builds Core only; lua must be disabled.")
        assert(not package:config("svg"), "MyEngine's RmlUi package builds Core only; svg must be disabled.")
        assert(not package:config("lottie"), "MyEngine's RmlUi package builds Core only; lottie must be disabled.")
        if package:config("freetype") then
            package:add("deps", "freetype", "zlib")
            package:add("defines", "RMLUI_FONT_ENGINE_FREETYPE")
        end
        if not package:config("shared") then
            package:add("defines", "RMLUI_STATIC_LIB")
        end
        package:add("defines", "RMLUI_MATRIX_ROW_MAJOR")
    end)

    on_install("windows", "macosx", "linux", function (package)
        local freetype_files = ""
        local freetype_requires = ""
        local freetype_packages = ""
        local freetype_define = ""
        if package:config("freetype") then
            freetype_files = [[
                add_files("Source/Core/FontEngineDefault/*.cpp")
            ]]
            freetype_requires = [[
            add_requires("freetype", "zlib")
            ]]
            freetype_packages = [[
                add_packages("freetype", "zlib")
            ]]
            freetype_define = [[
                add_defines("RMLUI_FONT_ENGINE_FREETYPE")
            ]]
        end

        local static_define = ""
        if not package:config("shared") then
            static_define = [[
                add_defines("RMLUI_STATIC_LIB")
            ]]
        end

        local rtti_flags = ""
        if not package:config("rtti") then
            rtti_flags = [[
                set_rtti(false)
                set_exceptions(false)
            ]]
        end

        io.writefile("xmake.lua", string.format([[
            add_rules("mode.debug", "mode.release")
            set_languages("c++17")
%s
            target("rmlui")
                set_kind("$(kind)")
%s
                add_includedirs("Include", "Source", "Source/Core", {public = true})
                add_headerfiles("Include/(RmlUi/Core.h)")
                add_headerfiles("Include/(RmlUi/Config/**.h)")
                add_headerfiles("Include/(RmlUi/Core/**.h)")
                add_headerfiles("Include/(RmlUi/Core/**.hpp)")
                add_headerfiles("Include/(RmlUi/Core/**.inl)")
                add_files("Source/Core/*.cpp")
                add_files("Source/Core/Elements/*.cpp")
                add_files("Source/Core/Layout/*.cpp")
%s
                add_defines("RMLUI_MATRIX_ROW_MAJOR")
                add_defines("RMLUI_VERSION=\"6.2\"")
%s
%s
%s
                if is_plat("windows") then
                    add_syslinks("shlwapi", "imm32", "user32")
                elseif is_plat("macosx") then
                    add_frameworks("Cocoa")
                end
        ]], freetype_requires, rtti_flags, freetype_files, static_define, freetype_define, freetype_packages))

        import("package.tools.xmake").install(package)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <RmlUi/Core.h>
            void test() {
                Rml::Context* context = Rml::CreateContext("default", Rml::Vector2i(640, 480));
                (void)context;
            }
        ]]}, {configs = {languages = "c++17"}}))
    end)
