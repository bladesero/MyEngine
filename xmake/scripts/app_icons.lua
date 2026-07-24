local incremental_files = import("scripts.incremental_files",
                                 {rootdir = path.join(os.projectdir(), "xmake")})

local function write_atomic(file, contents)
    local temporary = file .. "." .. hash.strhash32(contents .. tostring(os.time())) .. ".tmp"
    io.writefile(temporary, contents)
    if os.isfile(file) then
        os.rm(file)
    end
    os.mv(temporary, file)
end

function generate(tool)
    if not is_plat("windows") or not tool or not os.isfile(tool) then
        return
    end

    local outdir = path.join(os.projectdir(), "build", "generated", "icons")
    os.mkdir(outdir)
    local lock = io.openlock(path.join(outdir, "icons.lock"))
    lock:lock()

    local root = path.join(os.projectdir(), "EngineContent", "Editor", "Icons")
    local definitions = {
        {"engine-editor", "editor.ico"},
        {"engine-player", "player.ico"},
        {"engine-cooker", "cooker.ico"}
    }
    local fingerprint_parts = {hash.sha256(tool)}
    local outputs_exist = true
    for _, definition in ipairs(definitions) do
        table.insert(fingerprint_parts, definition[1])
        table.insert(fingerprint_parts,
                     hash.sha256(path.join(root, definition[1] .. ".svg")))
        if not os.isfile(path.join(outdir, definition[2])) then
            outputs_exist = false
        end
    end

    local fingerprint = hash.strhash128(table.concat(fingerprint_parts, "\n"))
    local stamp = path.join(outdir, "icons.stamp")
    if outputs_exist and os.isfile(stamp) and (io.readfile(stamp) or "") == fingerprint then
        lock:unlock()
        lock:close()
        return
    end

    for _, definition in ipairs(definitions) do
        local output = path.join(outdir, definition[2])
        local temporary = output .. "." .. fingerprint .. ".tmp"
        os.execv(tool, {"--icon-root", root, "--icon", definition[1],
                        "--output", temporary})
        incremental_files.copy_if_changed(temporary, output)
        os.rm(temporary)
    end
    write_atomic(stamp, fingerprint)
    lock:unlock()
    lock:close()
end
