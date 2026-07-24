local library_registry = {}

local valid_domains = {
    runtime = true,
    editor = true,
    thirdparty = true
}

local function copy_array(values)
    local result = {}
    for _, value in ipairs(values or {}) do
        table.insert(result, value)
    end
    return result
end

local function normalize_array(values, base)
    local result = {}
    for _, value in ipairs(values or {}) do
        local resolved = value
        if not path.is_absolute(resolved) then
            resolved = path.join(base, resolved)
        end
        table.insert(result, path.normalize(path.absolute(resolved)))
    end
    return result
end

local function target_values_array(target, name)
    local values = target:values(name)
    if not values then
        return {}
    end
    if type(values) == "string" then
        return {values}
    end
    return values
end

local function path_is_within(file, root)
    local normalized_file = path.normalize(path.absolute(file))
    local normalized_root = path.normalize(path.absolute(root))
    return normalized_file == normalized_root or
               normalized_file:startswith(normalized_root .. path.sep())
end

local function is_excluded(file, excluded_files, excluded_dirs)
    local normalized = path.normalize(path.absolute(file))
    if excluded_files[normalized] then
        return true
    end
    for _, directory in ipairs(excluded_dirs) do
        if path_is_within(normalized, directory) then
            return true
        end
    end
    return false
end

local function is_fragment(file, fragment_dirs)
    for _, directory in ipairs(fragment_dirs) do
        if path_is_within(file, directory) then
            return true
        end
    end
    return false
end

local function add_discovered_files(target)
    local roots = target_values_array(target, "myengine.module.roots")
    local legacy_root = target:values("myengine.module.root")
    if #roots == 0 and legacy_root then
        roots = {legacy_root}
    end
    if #roots == 0 then
        raise("target '" .. target:name() .. "' must declare an automatic source root")
    end

    local excluded_files = {}
    for _, file in ipairs(target_values_array(target, "myengine.module.excluded_files")) do
        excluded_files[path.normalize(path.absolute(file))] = true
    end
    local excluded_dirs = normalize_array(
                              target_values_array(target, "myengine.module.excluded_dirs"),
                              os.projectdir())
    local fragment_dirs = normalize_array(
                              target_values_array(target, "myengine.module.fragment_dirs"),
                              os.projectdir())

    local source_extensions = {".c", ".cc", ".cpp", ".cxx"}
    if is_plat("macosx") then
        table.insert(source_extensions, ".m")
        table.insert(source_extensions, ".mm")
    end
    if is_plat("windows") then
        table.insert(source_extensions, ".rc")
    end

    local seen_sources = {}
    local seen_headers = {}
    for _, root in ipairs(roots) do
        root = path.normalize(path.absolute(root))
        if not os.isdir(root) then
            raise("target '" .. target:name() ..
                      "' automatic source root does not exist: " .. root)
        end
        for _, extension in ipairs(source_extensions) do
            for _, sourcefile in ipairs(os.files(path.join(root, "**" .. extension))) do
                local normalized = path.normalize(path.absolute(sourcefile))
                if not seen_sources[normalized] and
                   not is_excluded(normalized, excluded_files, excluded_dirs) and
                   not is_fragment(normalized, fragment_dirs) then
                    target:add("files", normalized)
                    seen_sources[normalized] = true
                end
            end
        end

        for _, extension in ipairs({".h", ".hh", ".hpp", ".hxx", ".inl"}) do
            for _, headerfile in ipairs(os.files(path.join(root, "**" .. extension))) do
                local normalized = path.normalize(path.absolute(headerfile))
                if not seen_headers[normalized] and
                   not is_excluded(normalized, excluded_files, excluded_dirs) then
                    target:add("headerfiles", normalized, {public = true})
                    seen_headers[normalized] = true
                end
            end
        end
    end
end

local function add_packages_with_visibility(target, value_name, public)
    for _, package_name in ipairs(target_values_array(target, value_name)) do
        target:add("packages", package_name, {public = public})
    end
end

function myengine_library_registry()
    return library_registry
end

function myengine_library(opt)
    if type(opt) ~= "table" then
        raise("myengine_library expects a table")
    end
    if not opt.name or opt.name == "" then
        raise("myengine_library.name is required")
    end
    if not valid_domains[opt.domain] then
        raise("invalid myengine_library.domain for " .. opt.name)
    end
    if library_registry[opt.name] then
        raise("duplicate myengine_library declaration: " .. opt.name)
    end

    local roots = normalize_array(opt.roots, os.projectdir())
    if #roots == 0 then
        raise("myengine_library.roots is required for " .. opt.name)
    end
    local entry = {
        name = opt.name,
        domain = opt.domain,
        kind = opt.kind or "static",
        roots = roots,
        excluded_dirs = normalize_array(opt.excluded_dirs, os.projectdir()),
        excluded_files = normalize_array(opt.excluded_files, os.projectdir()),
        fragment_dirs = normalize_array(opt.fragment_dirs, os.projectdir()),
        deps = copy_array(opt.deps),
        public_packages = copy_array(opt.public_packages),
        private_packages = copy_array(opt.private_packages)
    }
    library_registry[entry.name] = entry

    target(entry.name)
        set_kind(entry.kind)
        if opt.basename then
            set_basename(opt.basename)
        end
        add_rules("myengine.module")
        for _, rule_name in ipairs(opt.rules or {}) do
            add_rules(rule_name)
        end
        set_values("myengine.architecture.role", "library")
        set_values("myengine.module.domain", entry.domain)
        set_values("myengine.module.roots", entry.roots)
        set_values("myengine.module.excluded_dirs", entry.excluded_dirs)
        set_values("myengine.module.excluded_files", entry.excluded_files)
        set_values("myengine.module.fragment_dirs", entry.fragment_dirs)
        set_values("myengine.module.public_packages", entry.public_packages)
        set_values("myengine.module.private_packages", entry.private_packages)

        for _, dependency in ipairs(entry.deps) do
            add_deps(dependency)
        end
        for _, language in ipairs(opt.languages or {}) do
            set_languages(language)
        end
        for _, includedir in ipairs(opt.includedirs or {}) do
            add_includedirs(includedir)
        end
        for _, includedir in ipairs(opt.public_includedirs or {}) do
            add_includedirs(includedir, {public = true})
        end
        for _, define in ipairs(opt.defines or {}) do
            add_defines(define)
        end
        for _, syslink in ipairs(opt.syslinks or {}) do
            add_syslinks(syslink)
        end
        for _, framework in ipairs(opt.frameworks or {}) do
            add_frameworks(framework)
        end
        if opt.warnings then
            set_warnings(opt.warnings)
        end
        if opt.configure then
            opt.configure()
        end
    target_end()
end

rule("myengine.module")
    if xmake.version():ge("3.0.0") then
        add_orders("myengine.architecture_gate", "myengine.module")
    else
        add_deps("myengine.architecture_gate", {order = true})
    end
    on_load(function(target)
        add_discovered_files(target)
        add_packages_with_visibility(target, "myengine.module.public_packages", true)
        add_packages_with_visibility(target, "myengine.module.private_packages", false)

        if target:values("myengine.module.domain") == "runtime" then
            target:add("defines", "MYENGINE_RUNTIME_EXPORTS")
            if not is_plat("windows") then
                target:add("cxflags", "-fvisibility=hidden")
                target:add("cxxflags", "-fvisibility-inlines-hidden")
                target:add("mxflags", "-fvisibility=hidden")
                target:add("mxxflags", "-fvisibility=hidden", "-fvisibility-inlines-hidden")
            end
        end
        if is_plat("windows") then
            target:add("cxflags", "/utf-8", {tools = "cl"})
        end
    end)
rule_end()

rule("myengine.runtime.abi")
    after_link(function(target)
        local runtime_abi = import("scripts.runtime_abi",
                                   {rootdir = path.join(os.projectdir(), "xmake")})
        runtime_abi.verify(target)
    end)
rule_end()
