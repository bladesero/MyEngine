local function export_symbols(target)
    local find_tool = import("lib.detect.find_tool")
    local toolchain = import("core.tool.toolchain")
    local msvc = toolchain.load("msvc", {plat = target:plat(), arch = target:arch()})
    assert(msvc and msvc:check(), "MSVC toolchain is required to validate Runtime exports")
    local dumpbin = assert(find_tool("dumpbin", {envs = msvc:runenvs()}), "dumpbin not found")
    local output = os.iorunv(dumpbin.program,
                             {"/exports", "/nologo", target:targetfile()},
                             {envs = msvc:runenvs()})
    local symbols = {}
    for _, line in ipairs(output:split("\n", {plain = true})) do
        local symbol = line:match("^%s*%d+%s+[%x]+%s+[%x]+%s+(%S+)")
        if symbol then
            symbols[symbol] = true
        end
    end
    local ordered = table.keys(symbols)
    table.sort(ordered)
    assert(#ordered > 0, "Runtime export validation found no explicit ABI symbols")
    return ordered
end

local function serialize(symbols)
    return table.concat(symbols, "\n") .. "\n"
end

local function write_if_changed(file, contents)
    if os.isfile(file) and (io.readfile(file) or ""):gsub("\r\n", "\n") == contents then
        return
    end
    os.mkdir(path.directory(file))
    local temporary = file .. "." .. hash.strhash32(contents .. tostring(os.time())) .. ".tmp"
    io.writefile(temporary, contents)
    if os.isfile(file) then
        os.rm(file)
    end
    os.mv(temporary, file)
end

local function summarize_difference(expected, actual)
    local expected_set = {}
    local actual_set = {}
    for _, symbol in ipairs(expected) do
        expected_set[symbol] = true
    end
    for _, symbol in ipairs(actual) do
        actual_set[symbol] = true
    end
    local added = {}
    local removed = {}
    for symbol in pairs(actual_set) do
        if not expected_set[symbol] then
            table.insert(added, symbol)
        end
    end
    for symbol in pairs(expected_set) do
        if not actual_set[symbol] then
            table.insert(removed, symbol)
        end
    end
    table.sort(added)
    table.sort(removed)
    local lines = {
        string.format("Runtime ABI differs from the approved baseline (%d added, %d removed)",
                      #added, #removed)
    }
    for index = 1, math.min(#added, 12) do
        table.insert(lines, "+ " .. added[index])
    end
    for index = 1, math.min(#removed, 12) do
        table.insert(lines, "- " .. removed[index])
    end
    return table.concat(lines, "\n")
end

function verify(target)
    if target:plat() ~= "windows" then
        return
    end
    local symbols = export_symbols(target)
    local contents = serialize(symbols)
    local mode = get_config("mode") or "debug"
    local filename =
        string.format("runtime-%s-%s-%s.exports", target:plat(), target:arch(), mode)
    local generated =
        path.join(os.projectdir(), "build", ".myengine", "abi", filename)
    local baseline_filename =
        string.format("runtime-%s-%s.exports", target:plat(), target:arch())
    local baseline = path.join(os.projectdir(), "xmake", "abi", baseline_filename)
    write_if_changed(generated, contents)
    assert(os.isfile(baseline),
           "Runtime ABI baseline is missing: " .. baseline ..
               "\nReview " .. generated .. " and add the approved baseline explicitly.")
    local expected_contents = (io.readfile(baseline) or ""):gsub("\r\n", "\n")
    if expected_contents ~= contents then
        local expected = expected_contents:split("\n", {plain = true})
        raise(summarize_difference(expected, symbols) ..
                  "\nReview generated manifest: " .. generated)
    end
end
