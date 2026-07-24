-- ---------------------------------------------------------------------------
-- Memory subsystem (ME_* heap helpers / future MemoryService)
-- Configure: xmake f --mem_stats=y|n --mem_tracking=y|n --mem_guard=y|n
-- Defaults: stats on; tracking+guard follow current mode (on in debug).
-- ---------------------------------------------------------------------------
option("mem_stats", function()
    set_default(true)
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_STATS: per-tag bytes / live counts / lifetime calls")
end)

option("mem_tracking", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_TRACKING: live allocation map + leak dump with file:line")
end)

option("mem_guard", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_GUARD: allocation poison fill + tail canaries")
end)

option("vulkan", function()
    set_default(false)
    set_showmenu(true)
    set_category("rendering")
    set_description("Compile the optional Windows Vulkan backend")
end)

function myengine_enable_vulkan()
    return is_plat("windows") and has_config("vulkan")
end

function myengine_feature_state()
    return {
        mem_stats = has_config("mem_stats"),
        mem_tracking = has_config("mem_tracking"),
        mem_guard = has_config("mem_guard"),
        vulkan = myengine_enable_vulkan()
    }
end

function myengine_feature_key()
    local state = myengine_feature_state()
    return string.format("mem_stats=%s;mem_tracking=%s;mem_guard=%s;vulkan=%s",
                         tostring(state.mem_stats), tostring(state.mem_tracking),
                         tostring(state.mem_guard), tostring(state.vulkan))
end
