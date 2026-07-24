local project_api
local library_registry
local package_catalog

local source_extensions = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".rc"}
local header_extensions = {".h", ".hh", ".hpp", ".hxx", ".inl"}
local scan_extensions = {
    ".c", ".cc", ".cpp", ".cxx", ".m", ".mm",
    ".h", ".hh", ".hpp", ".hxx", ".inl"
}

local engine_libraries = {
    ["MyEngine.ThirdParty"] = true,
    ["MyEngineRuntime"] = true,
    ["MyEngine.Editor"] = true
}

local function append_error(errors, code, detail)
    table.insert(errors, code .. ": " .. detail)
end

local function list_contains(values, expected)
    for _, value in ipairs(values or {}) do
        if value == expected then
            return true
        end
    end
    return false
end

local function array_set(values)
    local result = {}
    for _, value in ipairs(values or {}) do
        result[value] = true
    end
    return result
end

local function normalize(value)
    return path.normalize(path.absolute(value))
end

local function slash(value)
    return path.normalize(value):gsub("\\", "/")
end

local function path_contains(file, root)
    local normalized_file = slash(normalize(file))
    local normalized_root = slash(normalize(root))
    return normalized_file == normalized_root or
               normalized_file:startswith(normalized_root .. "/")
end

local function strip_cpp_comments(content)
    local stripped = content:gsub("/%*.-%*/", "")
    return stripped:gsub("//[^\r\n]*", "")
end

local function strip_lua_comments(content)
    local stripped = content:gsub("%-%-%[%[.-%]%]", "")
    return stripped:gsub("%-%-[^\r\n]*", "")
end

local function values_array(target, name)
    local values = target:values(name)
    if not values then
        return {}
    end
    if type(values) == "string" then
        return {values}
    end
    return values
end

local function validate_cycle(targets, errors)
    local visiting = {}
    local visited = {}

    local function visit(name, stack)
        if visiting[name] then
            append_error(errors, "dependency-cycle",
                         table.concat(stack, " -> ") .. " -> " .. name)
            return
        end
        if visited[name] then
            return
        end
        visiting[name] = true
        table.insert(stack, name)
        for _, dependency in ipairs((targets[name] or {}).deps or {}) do
            if targets[dependency] then
                visit(dependency, stack)
            end
        end
        table.remove(stack)
        visiting[name] = nil
        visited[name] = true
    end

    for name in pairs(targets) do
        visit(name, {})
    end
end

local function validate_library_edge(owner, dependency, errors)
    if owner == "MyEngine.ThirdParty" and
       (dependency == "MyEngineRuntime" or dependency == "MyEngine.Editor") then
        append_error(errors, "forbidden-edge", owner .. " -> " .. dependency)
    elseif owner == "MyEngineRuntime" and dependency == "MyEngine.Editor" then
        append_error(errors, "forbidden-edge", owner .. " -> " .. dependency)
    end
end

function validate_manifest(manifest)
    local errors = {}
    local targets = manifest.targets or manifest.modules or {}

    validate_cycle(targets, errors)
    for owner, metadata in pairs(targets) do
        for _, dependency in ipairs(metadata.deps or {}) do
            validate_library_edge(owner, dependency, errors)
        end
    end

    for name, expected_kind in pairs(manifest.required_libraries or {}) do
        local metadata = targets[name]
        if not metadata then
            append_error(errors, "library-missing", name)
        elseif metadata.kind ~= expected_kind then
            append_error(errors, "library-kind", name)
        end
    end

    for source, owners in pairs(manifest.source_owners or {}) do
        if #owners == 0 then
            append_error(errors, "source-unowned", source)
        elseif #owners > 1 then
            append_error(errors, "source-duplicate", source)
        end
    end

    for fragment, metadata in pairs(manifest.fragments or {}) do
        if metadata.translation_unit then
            append_error(errors, "fragment-translation-unit", fragment)
        end
        local includes = metadata.includes or {}
        if #includes == 0 then
            append_error(errors, "fragment-zero", fragment)
        elseif #includes > 1 then
            append_error(errors, "fragment-multiple", fragment)
        else
            local include = includes[1]
            if include.owner ~= metadata.owner then
                append_error(errors, "fragment-cross-library", fragment)
            end
            if include.fragment then
                append_error(errors, "fragment-chain", fragment)
            end
        end
    end

    local platform = manifest.platform
    for source, owners in pairs(manifest.platform_sources or {}) do
        local extension = source:match("(%.[^./\\]+)$")
        if ((extension == ".rc" and platform ~= "windows") or
            ((extension == ".m" or extension == ".mm") and platform ~= "macosx")) and
           #owners > 0 then
            append_error(errors, "platform-source", source)
        end
    end

    for source, content in pairs(manifest.sources or {}) do
        local normalized = slash(source)
        if (normalized:find("src/Runtime/", 1, true) or
            normalized:find("thirdparty/", 1, true)) and
           (content:find("#%s*include%s*[<\"]Editor/") or
            content:find("#%s*include%s*[<\"][^>\"]*src/Editor/")) then
            append_error(errors, "domain-leak", source)
        end

        local backend_owned = normalized:find("/Backends/", 1, true) ~= nil
        local native_header = content:find("#%s*include%s*[<\"]d3d") or
                                  content:find("#%s*include%s*[<\"]dxgi") or
                                  content:find("#%s*include%s*[<\"]wrl/") or
                                  content:find("#%s*include%s*[<\"]vulkan/") or
                                  content:find("#%s*import%s*[<\"]Metal/")
        if native_header and not backend_owned then
            append_error(errors, "backend-leak", source)
        end
    end

    for _, source in ipairs(manifest.tests_sources or {}) do
        if slash(source):find("src/Editor/", 1, true) then
            append_error(errors, "editor-source-duplicate", source)
        end
    end

    local vulkan = manifest.vulkan or {}
    if vulkan.enabled == false and
       (#(vulkan.runtime_sources or {}) > 0 or #(vulkan.editor_sources or {}) > 0 or
        #(vulkan.package_targets or {}) > 0) then
        append_error(errors, "vulkan-feature", "disabled Vulkan is still active")
    elseif vulkan.enabled == true and
           (#(vulkan.runtime_sources or {}) == 0 or #(vulkan.editor_sources or {}) == 0) then
        append_error(errors, "vulkan-feature", "enabled Vulkan is incomplete")
    end

    local define_owners = {}
    for owner, defines in pairs(manifest.defines or {}) do
        for _, define in ipairs(defines) do
            local name = define:match("^([^=]+)") or define
            if define_owners[name] then
                append_error(errors, "define-duplicate", name)
            end
            define_owners[name] = owner
        end
    end

    local root_script = manifest.root_script or ""
    local root_forbidden = {
        {"target%s*%(", "root-target"},
        {"add_files%s*%(", "root-source"},
        {"add_packages%s*%(", "root-package"},
        {"add_defines%s*%(", "root-define"},
        {"%.cpp", "root-source"},
        {"Editor", "root-editor"},
        {"D3D", "root-backend"},
        {"Vulkan", "root-backend"},
        {"Metal", "root-backend"}
    }
    for _, entry in ipairs(root_forbidden) do
        if root_script:find(entry[1]) then
            append_error(errors, entry[2], entry[1])
        end
    end
    return errors
end

local function run_fixture_tests()
    local fixtures =
        os.files(path.join(os.projectdir(), "tests", "ArchitectureFixture*.lua"))
    assert(#fixtures > 0, "architecture checker fixtures are missing")
    for _, fixture_file in ipairs(fixtures) do
        local fixture = io.load(fixture_file)
        local errors = validate_manifest(fixture.manifest)
        if fixture.expected_error then
            local matched = false
            for _, message in ipairs(errors) do
                if message:startswith(fixture.expected_error .. ":") then
                    matched = true
                    break
                end
            end
            assert(matched,
                   path.filename(fixture_file) .. " did not report " ..
                       fixture.expected_error)
        else
            assert(#errors == 0,
                   path.filename(fixture_file) .. " unexpectedly failed: " ..
                       table.concat(errors, "; "))
        end
    end
end

local function check_root_script()
    local root_script = io.readfile(path.join(os.projectdir(), "xmake.lua"))
    local errors = validate_manifest({root_script = root_script})
    if #errors > 0 then
        raise(table.concat(errors, "\n"))
    end
    for _, include in ipairs({
        "includes(\"xmake\")",
        "includes(\"thirdparty\")",
        "includes(\"src\")",
        "includes(\"tests\")"
    }) do
        if not root_script:find(include, 1, true) then
            raise("root-orchestration: missing " .. include)
        end
    end
end

local function build_scripts()
    local scripts = {path.join(os.projectdir(), "xmake.lua")}
    table.join2(scripts, os.files(path.join(os.projectdir(), "xmake", "**.lua")))
    table.join2(scripts,
                os.files(path.join(os.projectdir(), "src", "**", "xmake.lua")))
    table.join2(scripts,
                os.files(path.join(os.projectdir(), "tests", "**", "xmake.lua")))
    table.join2(scripts,
                os.files(path.join(os.projectdir(), "thirdparty", "**", "xmake.lua")))
    return scripts
end

local function check_central_declarations()
    local packages_script = normalize(path.join(os.projectdir(), "xmake", "packages.lua"))
    local build_config_script =
        normalize(path.join(os.projectdir(), "xmake", "build_config.lua"))
    local module_script = normalize(path.join(os.projectdir(), "xmake", "module.lua"))
    local options_script = normalize(path.join(os.projectdir(), "xmake", "options.lua"))
    for _, script in ipairs(build_scripts()) do
        local normalized = normalize(script)
        local content = strip_lua_comments(io.readfile(script) or "")
        if normalized ~= packages_script and content:find("add_requires%s*%(") then
            raise("package-owner: add_requires is only allowed in xmake/packages.lua: " ..
                      script)
        end
        if (path_contains(script, path.join(os.projectdir(), "src")) or
            path_contains(script, path.join(os.projectdir(), "tests")) or
            path_contains(script, path.join(os.projectdir(), "thirdparty"))) and
           content:find("add_files%s*%(") then
            raise("source-array: source scripts must use myengine.module discovery: " ..
                      script)
        end
        if normalized ~= build_config_script and normalized ~= module_script and
           normalized ~= options_script and
           (path_contains(script, path.join(os.projectdir(), "src")) or
            path_contains(script, path.join(os.projectdir(), "tests")) or
            path_contains(script, path.join(os.projectdir(), "thirdparty"))) and
           content:find("MYENGINE_[A-Z0-9_]+") then
            raise("define-owner: central MyEngine define outside approved owner: " ..
                      script)
        end
    end

    local declaration_files = {
        path.join(os.projectdir(), "thirdparty", "xmake.lua"),
        path.join(os.projectdir(), "src", "Runtime", "xmake.lua"),
        path.join(os.projectdir(), "src", "Editor", "xmake.lua")
    }
    for _, script in ipairs(declaration_files) do
        local content = io.readfile(script) or ""
        if not content:find("myengine_library%s*%(") then
            raise("library-declaration: missing myengine_library in " .. script)
        end
    end
end

local function target_dependencies(target)
    local result = {}
    for _, name in ipairs(target:get("deps") or {}) do
        table.insert(result, name)
    end
    return result
end

local function check_registered_targets()
    local expected = {
        ["MyEngine.ThirdParty"] = {kind = "static", domain = "thirdparty"},
        ["MyEngineRuntime"] = {kind = "shared", domain = "runtime"},
        ["MyEngine.Editor"] = {kind = "static", domain = "editor"}
    }
    local graph = {}
    local library_count = 0

    for name, target in pairs(project_api.targets()) do
        if name:startswith("MyEngine") then
            local role = target:values("myengine.architecture.role")
            if not role then
                raise("target-metadata-missing: " .. name)
            end
            graph[name] = {deps = target_dependencies(target)}
            for _, dependency in ipairs(graph[name].deps) do
                if dependency:startswith("MyEngine") and
                   not project_api.target(dependency) then
                    raise("target-missing: " .. name .. " -> " .. dependency)
                end
            end
            if role == "library" then
                library_count = library_count + 1
                if not expected[name] then
                    raise("library-unexpected: " .. name)
                end
            end
        end
    end

    if library_count ~= 3 then
        raise("library-count: expected 3 compiled engine libraries, got " ..
                  tostring(library_count))
    end
    for name, metadata in pairs(expected) do
        local entry = library_registry[name]
        local target = project_api.target(name)
        if not entry or not target then
            raise("library-missing: " .. name)
        end
        if target:kind() ~= metadata.kind or entry.kind ~= metadata.kind then
            raise("library-kind: " .. name)
        end
        if entry.domain ~= metadata.domain or
           target:values("myengine.module.domain") ~= metadata.domain then
            raise("library-domain: " .. name)
        end
        local declared = array_set(entry.deps)
        local actual = array_set(target_dependencies(target))
        for dependency in pairs(declared) do
            if not actual[dependency] then
                raise("dependency-missing: " .. name .. " -> " .. dependency)
            end
        end
        for dependency in pairs(actual) do
            if not declared[dependency] then
                raise("dependency-unregistered: " .. name .. " -> " .. dependency)
            end
        end
    end

    for name in pairs(graph) do
        if name:startswith("MyEngine.Runtime.") and
           name ~= "MyEngine.Runtime.API" then
            raise("fine-library-target: " .. name)
        end
        if name:startswith("MyEngine.Editor.") and
           name ~= "MyEngine.Editor.API" then
            raise("fine-library-target: " .. name)
        end
        if name:startswith("MyEngine.ThirdParty.") then
            raise("fine-library-target: " .. name)
        end
    end

    local errors = {}
    validate_cycle(graph, errors)
    if #errors > 0 then
        raise(table.concat(errors, "\n"))
    end
end

local function assert_library_dependencies(name, expected)
    local actual = {}
    for _, dependency in ipairs(target_dependencies(assert(project_api.target(name)))) do
        if engine_libraries[dependency] then
            actual[dependency] = true
        end
    end
    for dependency in pairs(expected) do
        if not actual[dependency] then
            raise("library-dependency-missing: " .. name .. " -> " .. dependency)
        end
    end
    for dependency in pairs(actual) do
        if not expected[dependency] then
            raise("forbidden-edge: " .. name .. " -> " .. dependency)
        end
    end
end

local function check_dependency_boundaries()
    assert_library_dependencies("MyEngine.ThirdParty", {})
    assert_library_dependencies("MyEngineRuntime", {["MyEngine.ThirdParty"] = true})
    assert_library_dependencies("MyEngine.Editor", {
        ["MyEngineRuntime"] = true,
        ["MyEngine.ThirdParty"] = true
    })

    local products = {
        MyEngineEditor = {
            ["MyEngineRuntime"] = true,
            ["MyEngine.Editor"] = true
        },
        MyEnginePlayer = {["MyEngineRuntime"] = true},
        MyEngineCooker = {
            ["MyEngineRuntime"] = true,
            ["MyEngine.Editor"] = true
        },
        MyEngineIconTool = {["MyEngineRuntime"] = true},
        MyEngineEditorPackager = {["MyEngineRuntime"] = true},
        MyEngineRuntimeLinkProbe = {["MyEngineRuntime"] = true},
        MyEngineTests = {
            ["MyEngineRuntime"] = true,
            ["MyEngine.Editor"] = true
        }
    }
    for product_name, expected in pairs(products) do
        local product = assert(project_api.target(product_name),
                               "product-missing: " .. product_name)
        local actual = {}
        for _, dependency in ipairs(target_dependencies(product)) do
            if engine_libraries[dependency] then
                actual[dependency] = true
            end
        end
        for dependency in pairs(expected) do
            if not actual[dependency] then
                raise("product-library-missing: " .. product_name .. " -> " ..
                          dependency)
            end
        end
        for dependency in pairs(actual) do
            if not expected[dependency] then
                raise("product-library-leak: " .. product_name .. " -> " ..
                          dependency)
            end
        end
    end
end

local function collect_files(root, extensions)
    local result = {}
    for _, extension in ipairs(extensions) do
        table.join2(result, os.files(path.join(root, "**" .. extension)))
    end
    return result
end

local function build_source_records()
    local records = {}
    for name, target in pairs(project_api.targets()) do
        local roots = values_array(target, "myengine.module.roots")
        local legacy_root = target:values("myengine.module.root")
        if #roots == 0 and legacy_root then
            roots = {legacy_root}
        end
        if #roots > 0 then
            local record = {
                target = name,
                roots = {},
                excluded_dirs = {},
                excluded_files = {},
                fragment_dirs = {}
            }
            for _, root in ipairs(roots) do
                table.insert(record.roots, normalize(root))
            end
            for _, directory in ipairs(
                values_array(target, "myengine.module.excluded_dirs")) do
                table.insert(record.excluded_dirs, normalize(directory))
            end
            for _, file in ipairs(
                values_array(target, "myengine.module.excluded_files")) do
                record.excluded_files[normalize(file)] = true
            end
            for _, directory in ipairs(
                values_array(target, "myengine.module.fragment_dirs")) do
                table.insert(record.fragment_dirs, normalize(directory))
            end
            table.insert(records, record)
        end
    end
    return records
end

local function file_in_directories(file, directories)
    for _, directory in ipairs(directories) do
        if path_contains(file, directory) then
            return true
        end
    end
    return false
end

local function platform_extension_active(file)
    local extension = path.extension(file):lower()
    if extension == ".rc" then
        return get_config("plat") == "windows"
    end
    if extension == ".m" or extension == ".mm" then
        return get_config("plat") == "macosx"
    end
    return true
end

local function active_for_record(file, record)
    local normalized = normalize(file)
    return platform_extension_active(normalized) and
               not record.excluded_files[normalized] and
               not file_in_directories(normalized, record.excluded_dirs) and
               not file_in_directories(normalized, record.fragment_dirs)
end

local function record_for_file(records, file)
    local best
    local best_length = -1
    for _, record in ipairs(records) do
        for _, root in ipairs(record.roots) do
            if path_contains(file, root) and #root > best_length then
                best = record
                best_length = #root
            end
        end
    end
    return best
end

local function build_source_state(records)
    local actual_owners = {}
    for target_name, target in pairs(project_api.targets()) do
        for _, sourcefile in ipairs(target:sourcefiles()) do
            local normalized = normalize(sourcefile)
            actual_owners[normalized] = actual_owners[normalized] or {}
            table.insert(actual_owners[normalized], target_name)
        end
    end

    local expected_owners = {}
    for _, record in ipairs(records) do
        for _, root in ipairs(record.roots) do
            for _, sourcefile in ipairs(collect_files(root, source_extensions)) do
                local normalized = normalize(sourcefile)
                if active_for_record(normalized, record) then
                    expected_owners[normalized] = expected_owners[normalized] or {}
                    table.insert(expected_owners[normalized], record.target)
                end
            end
        end
    end

    for sourcefile, expected in pairs(expected_owners) do
        if #expected > 1 then
            raise("source-root-overlap: " .. sourcefile)
        end
        local owners = actual_owners[sourcefile] or {}
        if #owners == 0 then
            raise("source-unowned: " .. sourcefile .. " expected " .. expected[1])
        elseif #owners > 1 then
            raise("source-duplicate: " .. sourcefile .. " -> " ..
                      table.concat(owners, ", "))
        elseif owners[1] ~= expected[1] then
            raise("source-owner: " .. sourcefile .. " -> " .. owners[1] ..
                      ", expected " .. expected[1])
        end
    end

    for sourcefile, owners in pairs(actual_owners) do
        local record = record_for_file(records, sourcefile)
        if record then
            if not active_for_record(sourcefile, record) then
                raise("platform-source: inactive source compiled by " ..
                          table.concat(owners, ", ") .. ": " .. sourcefile)
            end
            if #owners > 1 then
                raise("source-duplicate: " .. sourcefile)
            end
        elseif path_contains(sourcefile, path.join(os.projectdir(), "src")) or
               path_contains(sourcefile, path.join(os.projectdir(), "tests")) or
               path_contains(sourcefile, path.join(os.projectdir(), "thirdparty")) then
            raise("source-outside-root: " .. sourcefile)
        end
    end
    return actual_owners
end

local function resolve_include(sourcefile, include_path, records)
    local candidates = {
        path.join(path.directory(sourcefile), include_path),
        path.join(os.projectdir(), "src", include_path),
        path.join(os.projectdir(), include_path)
    }
    for _, record in ipairs(records) do
        for _, root in ipairs(record.roots) do
            table.insert(candidates, path.join(root, include_path))
        end
    end
    for _, candidate in ipairs(candidates) do
        local normalized = normalize(candidate)
        if os.isfile(normalized) then
            return normalized
        end
    end
    return nil
end

local function check_fragments(records)
    local fragments = {}
    local has_fragments = false
    for _, record in ipairs(records) do
        for _, directory in ipairs(record.fragment_dirs) do
            for _, sourcefile in ipairs(collect_files(directory, {
                ".c", ".cc", ".cpp", ".cxx", ".m", ".mm"
            })) do
                fragments[normalize(sourcefile)] = {
                    owner = record.target,
                    includes = {}
                }
                has_fragments = true
            end
        end
    end
    if not has_fragments then
        return
    end

    local scanned = {}
    for _, record in ipairs(records) do
        for _, root in ipairs(record.roots) do
            for _, sourcefile in ipairs(collect_files(root, source_extensions)) do
                local normalized = normalize(sourcefile)
                if not scanned[normalized] then
                    scanned[normalized] = true
                    local content = strip_cpp_comments(io.readfile(normalized) or "")
                    for include_path in
                        content:gmatch("#%s*include%s*[\"<]([^\">]+)[\">]") do
                        local included = resolve_include(normalized, include_path, records)
                        if included and fragments[included] then
                            local source_record = record_for_file(records, normalized)
                            table.insert(fragments[included].includes, {
                                owner = source_record and source_record.target or "",
                                fragment = file_in_directories(
                                    normalized,
                                    source_record and source_record.fragment_dirs or {})
                            })
                        end
                    end
                end
            end
        end
    end

    for fragment, metadata in pairs(fragments) do
        if #metadata.includes == 0 then
            raise("fragment-zero: " .. fragment)
        elseif #metadata.includes > 1 then
            raise("fragment-multiple: " .. fragment)
        elseif metadata.includes[1].owner ~= metadata.owner then
            raise("fragment-cross-library: " .. fragment)
        elseif metadata.includes[1].fragment then
            raise("fragment-chain: " .. fragment)
        end
    end
end

local function check_domain_and_backend_includes()
    local editor_root = path.join(os.projectdir(), "src", "Editor")
    for _, entry in pairs(library_registry) do
        for _, root in ipairs(entry.roots) do
            for _, file in ipairs(collect_files(root, scan_extensions)) do
                local normalized = normalize(file)
                local content = strip_cpp_comments(io.readfile(normalized) or "")
                for include_path in
                    content:gmatch("#%s*include%s*[\"<]([^\">]+)[\">]") do
                    if (entry.domain == "runtime" or entry.domain == "thirdparty") then
                        local resolved = normalize(path.join(path.directory(normalized),
                                                             include_path))
                        if include_path:startswith("Editor/") or
                           path_contains(resolved, editor_root) then
                            raise("domain-leak: " .. entry.name .. " includes " ..
                                      include_path .. " from " .. normalized)
                        end
                    end
                end

                local backend_owned = slash(normalized):find("/Backends/", 1, true)
                local native_header = content:find("#%s*include%s*[<\"]d3d") or
                                          content:find("#%s*include%s*[<\"]dxgi") or
                                          content:find("#%s*include%s*[<\"]wrl/") or
                                          content:find("#%s*include%s*[<\"]vulkan/") or
                                          content:find("#%s*import%s*[<\"]Metal/")
                if native_header and not backend_owned then
                    raise("backend-leak: native backend header outside Backends: " ..
                              normalized)
                end
            end
        end
    end
end

local function target_sources_under(target, directory)
    local result = {}
    for _, sourcefile in ipairs(target:sourcefiles()) do
        if path_contains(sourcefile, directory) then
            table.insert(result, normalize(sourcefile))
        end
    end
    return result
end

local function check_vulkan_state(features)
    local runtime = assert(project_api.target("MyEngineRuntime"))
    local editor = assert(project_api.target("MyEngine.Editor"))
    local runtime_sources = target_sources_under(
                                runtime,
                                path.join(os.projectdir(), "src", "Runtime",
                                          "Renderer", "Backends", "Vulkan"))
    local editor_sources = target_sources_under(
                               editor,
                               path.join(os.projectdir(), "src", "Editor",
                                         "Backends", "Vulkan"))
    local runtime_packages =
        array_set(values_array(runtime, "myengine.module.private_packages"))
    local editor_packages =
        array_set(values_array(editor, "myengine.module.private_packages"))

    if features.vulkan then
        if #runtime_sources == 0 or #editor_sources == 0 then
            raise("vulkan-feature: enabled Vulkan source roots are incomplete")
        end
        if not runtime_packages["vulkan-headers"] or
           not editor_packages["vulkan-headers"] then
            raise("vulkan-feature: enabled Vulkan package is missing")
        end
    else
        if #runtime_sources > 0 or #editor_sources > 0 then
            raise("vulkan-feature: disabled Vulkan source is active")
        end
        if runtime_packages["vulkan-headers"] or editor_packages["vulkan-headers"] then
            raise("vulkan-feature: disabled Vulkan package is active")
        end
    end
end

local function check_package_declarations(features)
    for name, entry in pairs(library_registry) do
        local target = assert(project_api.target(name))
        local declared = {}
        for _, value_name in ipairs({
            "myengine.module.public_packages",
            "myengine.module.private_packages"
        }) do
            for _, package_name in ipairs(values_array(target, value_name)) do
                if declared[package_name] then
                    raise("package-duplicate: " .. name .. ":" .. package_name)
                end
                local package = package_catalog[package_name]
                if not package then
                    raise("package-unknown: " .. name .. ":" .. package_name)
                end
                if package.enabled and not package.enabled() then
                    raise("package-disabled: " .. name .. ":" .. package_name)
                end
                declared[package_name] = true
            end
        end
    end
    check_vulkan_state(features)
end

local function check_central_defines(features)
    local owners = {}
    for _, target in pairs(project_api.targets()) do
        local owner = target:values("myengine.central_define_owner")
        if owner then
            for _, define in ipairs(target:values("myengine.central_defines") or {}) do
                local name = define:match("^([^=]+)") or define
                if owners[name] then
                    raise("define-duplicate: " .. name .. " in " .. owners[name] ..
                              " and " .. owner)
                end
                owners[name] = owner
            end
        end
    end
    if features.vulkan ~= (owners.MYENGINE_ENABLE_VULKAN ~= nil) then
        raise("define-feature-drift: MYENGINE_ENABLE_VULKAN")
    end
    if owners.MYENGINE_ENABLE_IMGUI ~= "EditorAPI" then
        raise("define-owner: MYENGINE_ENABLE_IMGUI")
    end
end

local function check_runtime_abi()
    local module_script = io.readfile(path.join(os.projectdir(), "xmake", "module.lua")) or ""
    if module_script:find("runtime.export_all", 1, true) or
       module_script:find('"/symbols"', 1, true) or
       module_script:find("WHOLEARCHIVE", 1, true) or
       module_script:find("whole%-archive") then
        raise("runtime-abi: export-all and archive aggregation are forbidden")
    end

    local runtime_root = path.join(os.projectdir(), "src", "Runtime")
    for _, header in ipairs(collect_files(runtime_root, header_extensions)) do
        local content = io.readfile(header) or ""
        if content:find("MYENGINE_RUNTIME_API", 1, true) and
           not slash(header):endswith("/API/RuntimeApi.h") and
           not content:find('#include "API/RuntimeApi.h"', 1, true) then
            raise("runtime-abi-header: exported header is not self-contained: " ..
                      header)
        end
        if slash(header):find("/Renderer/Backends/", 1, true) and
           content:find("MYENGINE_RUNTIME_API", 1, true) then
            raise("runtime-abi-backend: backend implementation is exported: " .. header)
        end
    end

    for _, baseline in
        ipairs(os.files(path.join(os.projectdir(), "xmake", "abi", "**.exports"))) do
        local previous
        local seen = {}
        for symbol in (io.readfile(baseline) or ""):gmatch("[^\r\n]+") do
            if seen[symbol] then
                raise("runtime-abi-baseline: duplicate symbol in " .. baseline)
            end
            if previous and previous > symbol then
                raise("runtime-abi-baseline: symbols are not sorted in " .. baseline)
            end
            seen[symbol] = true
            previous = symbol
        end
    end
end

local function check_product_boundaries()
    local runtime = assert(project_api.target("MyEngineRuntime"))
    if runtime:kind() ~= "shared" or runtime:get("basename") ~= "runtime" then
        raise("runtime-boundary: MyEngineRuntime must remain runtime shared library")
    end
    for _, rule_name in ipairs({"myengine.module", "myengine.runtime.abi"}) do
        if not runtime:rule(rule_name) then
            raise("runtime-boundary: MyEngineRuntime is missing rule " .. rule_name)
        end
    end
    if runtime:rule("myengine.runtime.aggregate") then
        raise("runtime-boundary: static module aggregation must be removed")
    end

    for _, product_name in ipairs({
        "MyEngineEditor",
        "MyEnginePlayer",
        "MyEngineCooker",
        "MyEngineIconTool",
        "MyEngineEditorPackager",
        "MyEngineRuntimeLinkProbe",
        "MyEngineTests"
    }) do
        local product = assert(project_api.target(product_name),
                               "product-missing: " .. product_name)
        if product:dep("MyEngine.Architecture") then
            raise("architecture-phony-dependency: " .. product_name)
        end
        if not product:rule("myengine.module") then
            raise("architecture-gate-missing: " .. product_name)
        end
    end

    local tests = assert(project_api.target("MyEngineTests"))
    for _, sourcefile in ipairs(tests:sourcefiles()) do
        if slash(sourcefile):find("/src/Editor/", 1, true) then
            raise("editor-source-duplicate: MyEngineTests compiles " .. sourcefile)
        end
    end
    if project_api.target("MyEngine.Editor.Cooking") then
        raise("fine-library-target: MyEngine.Editor.Cooking")
    end
end

function check_project(project_instance, registry, packages, features)
    project_api = project_instance
    library_registry =
        assert(registry, "architecture checker requires the library registry")
    package_catalog = assert(packages, "architecture checker requires package catalog")
    features = assert(features, "architecture checker requires canonical feature state")

    run_fixture_tests()
    check_root_script()
    check_central_declarations()
    check_registered_targets()
    check_dependency_boundaries()
    local records = build_source_records()
    build_source_state(records)
    check_fragments(records)
    check_domain_and_backend_includes()
    check_package_declarations(features)
    check_central_defines(features)
    check_runtime_abi()
    check_product_boundaries()
end

function write_stamp_atomic(stamp, fingerprint)
    os.mkdir(path.directory(stamp))
    local temporary =
        stamp .. "." .. hash.strhash32(tostring(os.time()) .. fingerprint) .. ".tmp"
    io.writefile(temporary, fingerprint)
    if os.isfile(stamp) then
        os.rm(stamp)
    end
    os.mv(temporary, stamp)
end

function check_and_stamp(stamp, fingerprint, force, project_instance, registry, packages,
                         features)
    os.mkdir(path.directory(stamp))
    local lock = io.openlock(stamp .. ".lock")
    lock:lock()
    if not force and os.isfile(stamp) and (io.readfile(stamp) or "") == fingerprint then
        lock:unlock()
        lock:close()
        return
    end

    check_project(project_instance, registry, packages, features)
    write_stamp_atomic(stamp, fingerprint)
    lock:unlock()
    lock:close()
end
