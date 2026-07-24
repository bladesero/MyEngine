set_project("MyEngine")
set_version("0.1.0")
set_xmakever("2.8.0")

add_repositories("myengine-packages .", {rootdir = os.scriptdir()})

add_rules("mode.debug", "mode.release")
set_languages("c++17")
set_warnings("all")
if is_mode("release") then
    set_symbols("debug")
    set_strip("none")
end

includes("xmake")
includes("thirdparty")
includes("src")
includes("tests")
