local checked_fingerprints = {}

local function architecture_inputs()
    local files = {path.join(os.projectdir(), "xmake.lua")}
    local patterns = {
        path.join(os.projectdir(), "xmake", "**.lua"),
        path.join(os.projectdir(), "xmake", "abi", "**.exports"),
        path.join(os.projectdir(), "packages", "**", "xmake.lua"),
        path.join(os.projectdir(), "src", "**", "xmake.lua"),
        path.join(os.projectdir(), "tests", "**.lua"),
        path.join(os.projectdir(), "thirdparty", "**", "xmake.lua")
    }
    for _, extension in ipairs({
        ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".rc",
        ".h", ".hh", ".hpp", ".hxx", ".inl"
    }) do
        table.insert(patterns, path.join(os.projectdir(), "src", "**" .. extension))
    end
    for _, pattern in ipairs(patterns) do
        table.join2(files, os.files(pattern))
    end

    local unique = {}
    local result = {}
    for _, file in ipairs(files) do
        local normalized = path.normalize(path.absolute(file))
        if os.isfile(normalized) and not unique[normalized] then
            unique[normalized] = true
            table.insert(result, normalized)
        end
    end
    table.sort(result)
    return result
end

local function architecture_fingerprint(target)
    local parts = {
        "xmake=" .. tostring(xmake.version()),
        "plat=" .. tostring(target and target:plat() or get_config("plat")),
        "arch=" .. tostring(target and target:arch() or get_config("arch")),
        "mode=" .. tostring(get_config("mode")),
        "features=" .. myengine_feature_key()
    }
    for _, file in ipairs(architecture_inputs()) do
        local relative = path.relative(file, os.projectdir()):gsub("\\", "/")
        table.insert(parts, relative)
        local digest = hash.sha256(file)
        table.insert(parts, digest)
    end
    return hash.strhash128(table.concat(parts, "\n"))
end

local function architecture_stamp(target)
    local key = table.concat({
        tostring(target and target:plat() or get_config("plat")),
        tostring(target and target:arch() or get_config("arch")),
        tostring(get_config("mode")),
        hash.strhash32(myengine_feature_key())
    }, "-")
    return path.join(os.projectdir(), "build", ".myengine", "architecture", key .. ".stamp")
end

local function run_architecture_check(target, force, project, checker)
    local fingerprint = architecture_fingerprint(target)
    if not force and checked_fingerprints[fingerprint] then
        return
    end

    local stamp = architecture_stamp(target)
    if not force and os.isfile(stamp) and (io.readfile(stamp) or "") == fingerprint then
        checked_fingerprints[fingerprint] = true
        return
    end

    checker.check_and_stamp(stamp, fingerprint, force, project, myengine_library_registry(),
                            myengine_package_catalog(), myengine_feature_state())
    checked_fingerprints[fingerprint] = true
end

rule("myengine.architecture_gate")
    before_build(function(target)
        import("core.project.project")
        local checker = import("scripts.architecture",
                               {rootdir = path.join(os.projectdir(), "xmake")})
        run_architecture_check(target, false, project, checker)
    end)
rule_end()

target("MyEngine.Architecture")
    set_kind("phony")
    set_values("myengine.architecture.role", "validation")
    on_build(function(target)
        import("core.project.project")
        local checker = import("scripts.architecture",
                               {rootdir = path.join(os.projectdir(), "xmake")})
        run_architecture_check(target, true, project, checker)
    end)
target_end()
