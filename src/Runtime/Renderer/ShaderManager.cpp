#include "Renderer/ShaderManager.h"

#include "Core/Logger.h"

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#endif

#include <sstream>
#include <fstream>
#include <filesystem>

namespace {

std::filesystem::path ResolveShaderPath(const std::string& path)
{
    namespace fs = std::filesystem;
    fs::path requested(path);
    if (requested.is_absolute() || fs::exists(requested)) return requested;

    fs::path cursor = fs::current_path();
    while (!cursor.empty()) {
        const fs::path candidate = cursor / requested;
        if (fs::exists(candidate)) return candidate;
        const fs::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return requested;
}

} // namespace

ShaderManager& ShaderManager::Get() {
    static ShaderManager g_Instance;
    return g_Instance;
}

std::string ShaderManager::MakeKey(
    const std::string& shaderPath,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount) {
    std::ostringstream oss;
    oss << shaderPath << "|" << vsEntry << "|" << psEntry
        << "|layout=" << reinterpret_cast<uintptr_t>(layout)
        << "|count=" << layoutCount;
    return oss.str();
}

std::shared_ptr<GpuShader> ShaderManager::CompileRecord(const ShaderRecord& rec) {
    if (!m_Context) return nullptr;
    const std::filesystem::path resolvedPath = ResolveShaderPath(rec.path);
    const std::string resolvedPathString = resolvedPath.string();

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (m_Context->GetBackend() == RHIBackend::D3D11) {
        D3D11CompiledShaderProgram program{};
        if (!ShaderCompilerD3D11::CompileProgramFromFile(
                resolvedPathString, rec.vsEntry, rec.psEntry, program)) {
            return nullptr;
        }
        return m_Context->CreateShaderFromBytecode(
            program.vsBytecode.data(), program.vsBytecode.size(),
            program.psBytecode.data(), program.psBytecode.size(),
            rec.layout, rec.layoutCount);
    }
    if (m_Context->GetBackend() == RHIBackend::D3D12) {
        D3D12CompiledShaderProgram program{};
        if (!ShaderCompilerD3D12::CompileProgramFromFile(
                resolvedPathString, rec.vsEntry, rec.psEntry, program)) {
            return nullptr;
        }
        return m_Context->CreateShaderFromBytecode(
            program.vsBytecode.data(), program.vsBytecode.size(),
            program.psBytecode.data(), program.psBytecode.size(),
            rec.layout, rec.layoutCount);
    }
#endif
    std::ifstream input(resolvedPath, std::ios::binary);
    if (!input.is_open()) return nullptr;
    std::ostringstream source;
    source << input.rdbuf();
    return m_Context->CreateShader(
        source.str(), rec.vsEntry, rec.psEntry, rec.layout, rec.layoutCount);
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreate(
    const std::string& shaderPath,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount) {
    const std::string key = MakeKey(shaderPath, vsEntry, psEntry, layout, layoutCount);
    auto it = m_KeyToIndex.find(key);
    if (it != m_KeyToIndex.end()) {
        return m_Records[it->second].handle;
    }

    ShaderRecord rec{};
    rec.key = key;
    rec.path = shaderPath;
    rec.vsEntry = vsEntry;
    rec.psEntry = psEntry;
    rec.layout = layout;
    rec.layoutCount = layoutCount;
    rec.handle = std::make_shared<ShaderHandle>();

    rec.handle->shader = CompileRecord(rec);
    if (rec.handle->shader) {
        rec.handle->version++;
    } else {
        Logger::Error("[ShaderCompileError] file=", shaderPath,
            " entry=(vs:", vsEntry, " ps:", psEntry, ") message=initial compile failed");
    }

    const size_t idx = m_Records.size();
    m_Records.push_back(rec);
    m_KeyToIndex.emplace(key, idx);
    return rec.handle;
}

void ShaderManager::Recompile(const std::string& shaderPath) {
    for (auto& rec : m_Records) {
        if (!shaderPath.empty() && rec.path != shaderPath) {
            continue;
        }
        const auto newShader = CompileRecord(rec);
        if (!newShader) {
            Logger::Error("[ShaderCompileError] file=", rec.path,
                " entry=(vs:", rec.vsEntry, " ps:", rec.psEntry,
                ") message=recompile failed, kept previous GPU shader");
            continue;
        }
        rec.handle->shader = newShader;
        rec.handle->version++;
        Logger::Info("[ShaderManager] Recompiled: ", rec.path);
    }
}

void ShaderManager::Clear() {
    m_Records.clear();
    m_KeyToIndex.clear();
    m_Context = nullptr;
}
