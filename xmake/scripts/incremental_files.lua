local function normalized_absolute(file)
    return path.normalize(path.absolute(file))
end

local function digest(file)
    return hash.sha256(file)
end

local function read_manifest(manifest)
    local entries = {}
    if not os.isfile(manifest) then
        return entries
    end
    for line in (io.readfile(manifest) or ""):gmatch("[^\r\n]+") do
        local relative, file_digest = line:match("^([^\t]+)\t([0-9a-fA-F]+)$")
        if relative and file_digest then
            entries[relative] = file_digest:lower()
        end
    end
    return entries
end

local function serialize_manifest(entries)
    local relative_paths = table.keys(entries)
    table.sort(relative_paths)
    local lines = {}
    for _, relative in ipairs(relative_paths) do
        table.insert(lines, relative .. "\t" .. entries[relative])
    end
    return table.concat(lines, "\n") .. (#lines > 0 and "\n" or "")
end

function copy_if_changed(source, destination)
    source = normalized_absolute(source)
    destination = normalized_absolute(destination)
    if os.isfile(destination) and os.filesize(source) == os.filesize(destination) and
       digest(source) == digest(destination) then
        return false
    end
    os.mkdir(path.directory(destination))
    os.cp(source, destination)
    return true
end

local function sync_locked(roots, destination_root, manifest)
    local previous = read_manifest(manifest)
    local current = {}

    for _, root in ipairs(roots) do
        local source_root = normalized_absolute(root.source)
        if os.isdir(source_root) then
            for _, source in ipairs(os.files(path.join(source_root, "**"))) do
                local suffix = path.relative(source, source_root):gsub("\\", "/")
                local relative = path.join(root.destination, suffix):gsub("\\", "/")
                local destination = path.join(destination_root, relative)
                local source_digest = digest(source):lower()
                current[relative] = source_digest
                if previous[relative] ~= source_digest or not os.isfile(destination) then
                    os.mkdir(path.directory(destination))
                    os.cp(source, destination)
                end
            end
        end
    end

    for relative in pairs(previous) do
        if not current[relative] then
            local stale = normalized_absolute(path.join(destination_root, relative))
            local managed_root = normalized_absolute(destination_root)
            if stale:sub(1, #managed_root + 1) ~= managed_root .. path.sep() then
                raise("incremental manifest escaped destination root: " .. relative)
            end
            if os.isfile(stale) then
                os.rm(stale)
            end
        end
    end

    local serialized = serialize_manifest(current)
    if os.isfile(manifest) and (io.readfile(manifest) or "") == serialized then
        return
    end
    os.mkdir(path.directory(manifest))
    local temporary = manifest .. "." .. hash.strhash32(serialized .. tostring(os.time())) .. ".tmp"
    io.writefile(temporary, serialized)
    if os.isfile(manifest) then
        os.rm(manifest)
    end
    os.mv(temporary, manifest)
end

function sync_managed_directories(roots, destination_root, manifest)
    destination_root = normalized_absolute(destination_root)
    manifest = normalized_absolute(manifest)
    os.mkdir(path.directory(manifest))
    local lock = io.openlock(manifest .. ".lock")
    lock:lock()
    sync_locked(roots, destination_root, manifest)
    lock:unlock()
    lock:close()
end
