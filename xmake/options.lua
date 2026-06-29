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
    add_defines("MYENGINE_MEM_STATS")
end)

option("mem_tracking", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_TRACKING: live allocation map + leak dump with file:line")
    add_defines("MYENGINE_MEM_TRACKING")
end)

option("mem_guard", function()
    set_default(is_mode("debug"))
    set_showmenu(true)
    set_category("memory")
    set_description("MYENGINE_MEM_GUARD: allocation poison fill + tail canaries")
    add_defines("MYENGINE_MEM_GUARD")
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
