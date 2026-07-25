#include "PlatformShaderCompiler.h"

#include <atomic>

namespace {
std::atomic<D3DShaderStageCompiler> g_D3DShaderStageCompiler{nullptr};
}

bool RegisterD3DShaderStageCompiler(D3DShaderStageCompiler compiler) {
    if (!compiler)
        return false;

    D3DShaderStageCompiler expected = nullptr;
    return g_D3DShaderStageCompiler.compare_exchange_strong(expected, compiler, std::memory_order_release,
                                                            std::memory_order_acquire) ||
           expected == compiler;
}

bool HasD3DShaderStageCompiler() {
    return g_D3DShaderStageCompiler.load(std::memory_order_acquire) != nullptr;
}

bool CompileD3DShaderStage(bool d3d12, const std::string& filePath, const std::string& entry, const char* profile,
                           std::vector<unsigned char>& outBytecode, const std::vector<std::string>& defines) {
    const auto compiler = g_D3DShaderStageCompiler.load(std::memory_order_acquire);
    return compiler && compiler(d3d12, filePath, entry, profile, outBytecode, defines);
}
