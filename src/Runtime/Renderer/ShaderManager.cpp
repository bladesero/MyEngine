#include "Renderer/ShaderManager.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/ShaderCompilerD3D11.h"
#include "Renderer/ShaderCompilerD3D12.h"
#endif
#include <fstream>
#include <sstream>

ShaderManager& ShaderManager::Get() { static ShaderManager instance; return instance; }

std::string ShaderManager::MakeKey(const ShaderAsset& asset, const VertexElement* layout,
                                   uint32_t count, bool compute) const {
    std::ostringstream out;
    out << asset.GetID() << ':' << asset.GetVersion() << ':'
        << static_cast<int>(m_Context ? m_Context->GetBackend() : RHIBackend::Unknown)
        << ':' << compute;
    for (uint32_t i = 0; i < count; ++i) {
        out << '|' << layout[i].semantic << ':' << layout[i].index << ':'
            << static_cast<int>(layout[i].format) << ':' << layout[i].offset;
    }
    return out.str();
}

std::shared_ptr<GpuShader> ShaderManager::CompileRecord(const ShaderRecord& rec) {
    if (!m_Context || !rec.asset.IsValid()) return {};
    const ShaderAsset& asset = *rec.asset;
    const RHIBackend activeBackend = m_Context->GetBackend();
    const ShaderBackend backend = activeBackend == RHIBackend::D3D12
        ? ShaderBackend::D3D12 : ShaderBackend::D3D11;
    if (asset.IsCooked()) {
        if (rec.compute) {
            const auto& cs = asset.GetBytecode(backend, ShaderStage::Compute);
            return cs.empty() ? nullptr : m_Context->CreateComputeShaderFromBytecode(cs.data(), cs.size());
        }
        const auto& vs = asset.GetBytecode(backend, ShaderStage::Vertex);
        const auto& ps = asset.GetBytecode(backend, ShaderStage::Pixel);
        if (vs.empty() || ps.empty()) return {};
        return m_Context->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(),
            rec.layout.data(), static_cast<uint32_t>(rec.layout.size()));
    }
    if (activeBackend != RHIBackend::D3D11 && activeBackend != RHIBackend::D3D12) {
        if (rec.compute) return {};
        std::ifstream input(asset.ResolveSource(ShaderStage::Vertex), std::ios::binary);
        std::ostringstream source; source << input.rdbuf();
        return input ? m_Context->CreateShader(source.str(), asset.GetStage(ShaderStage::Vertex).entry,
            asset.GetStage(ShaderStage::Pixel).entry, rec.layout.data(), static_cast<uint32_t>(rec.layout.size())) : nullptr;
    }
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (rec.compute) {
        std::vector<unsigned char> cs;
        const auto& stage = asset.GetStage(ShaderStage::Compute);
        const char* profile = backend == ShaderBackend::D3D12 ? "cs_5_1" : "cs_5_0";
        const bool ok = backend == ShaderBackend::D3D12
            ? ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Compute).string(), stage.entry, profile, cs, asset.GetDefines())
            : ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Compute).string(), stage.entry, profile, cs, asset.GetDefines());
        return ok ? m_Context->CreateComputeShaderFromBytecode(cs.data(), cs.size()) : nullptr;
    }
    const auto& vsStage = asset.GetStage(ShaderStage::Vertex);
    const auto& psStage = asset.GetStage(ShaderStage::Pixel);
    std::vector<unsigned char> vs, ps;
    const bool ok = backend == ShaderBackend::D3D12
        ? ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Vertex).string(), vsStage.entry, "vs_5_1", vs, asset.GetDefines()) &&
          ShaderCompilerD3D12::CompileStageFromFile(asset.ResolveSource(ShaderStage::Pixel).string(), psStage.entry, "ps_5_1", ps, asset.GetDefines())
        : ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Vertex).string(), vsStage.entry, "vs_5_0", vs, asset.GetDefines()) &&
          ShaderCompilerD3D11::CompileStageFromFile(asset.ResolveSource(ShaderStage::Pixel).string(), psStage.entry, "ps_5_0", ps, asset.GetDefines());
    return ok ? m_Context->CreateShaderFromBytecode(vs.data(), vs.size(), ps.data(), ps.size(),
        rec.layout.data(), static_cast<uint32_t>(rec.layout.size())) : nullptr;
#else
    if (rec.compute) return {};
    std::ifstream input(asset.ResolveSource(ShaderStage::Vertex), std::ios::binary);
    std::ostringstream source; source << input.rdbuf();
    return input ? m_Context->CreateShader(source.str(), asset.GetStage(ShaderStage::Vertex).entry,
        asset.GetStage(ShaderStage::Pixel).entry, rec.layout.data(), static_cast<uint32_t>(rec.layout.size())) : nullptr;
#endif
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateInternal(
    const std::string& path, const VertexElement* layout, uint32_t count, bool compute) {
    auto asset = AssetManager::Get().Load<ShaderAsset>(path);
    if (!asset.IsValid() || asset->IsCompute() != compute) {
        Logger::Error("[ShaderManager] Invalid shader asset/type: ", path); return {};
    }
    const std::string key = MakeKey(*asset, layout, count, compute);
    if (auto it = m_KeyToIndex.find(key); it != m_KeyToIndex.end()) return m_Records[it->second].handle;
    ShaderRecord rec; rec.key = key; rec.path = path; rec.asset = asset; rec.compute = compute;
    if (layout && count) rec.layout.assign(layout, layout + count);
    rec.handle = std::make_shared<ShaderHandle>(); rec.handle->shader = CompileRecord(rec);
    if (rec.handle->shader) ++rec.handle->version;
    else Logger::Error("[ShaderManager] Initial compile failed: ", path);
    const size_t index = m_Records.size(); m_Records.push_back(std::move(rec)); m_KeyToIndex[key] = index;
    return m_Records.back().handle;
}

std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreate(
    const std::string& path, const VertexElement* layout, uint32_t count) {
    return GetOrCreateInternal(path, layout, count, false);
}
std::shared_ptr<ShaderHandle> ShaderManager::GetOrCreateCompute(const std::string& path) {
    return GetOrCreateInternal(path, nullptr, 0, true);
}

void ShaderManager::Recompile(const std::string& path) {
    if (!path.empty()) AssetManager::Get().Reload(path);
    for (auto& rec : m_Records) {
        if (!path.empty() && rec.path != path) continue;
        if (path.empty()) AssetManager::Get().Reload(rec.path);
        auto refreshed = AssetManager::Get().Load<ShaderAsset>(rec.path);
        if (refreshed.IsValid()) rec.asset = refreshed;
        auto shader = CompileRecord(rec);
        if (!shader) { Logger::Error("[ShaderManager] Recompile failed; retained old shader: ", rec.path); continue; }
        rec.handle->shader = std::move(shader); ++rec.handle->version;
    }
}
void ShaderManager::Clear() { m_Records.clear(); m_KeyToIndex.clear(); m_Context = nullptr; }
