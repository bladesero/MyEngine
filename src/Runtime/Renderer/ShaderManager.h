#pragma once

#include "Renderer/IRenderContext.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderHandle {
    std::shared_ptr<GpuShader> shader;
    uint64_t version = 0;
};

class ShaderManager {
public:
    static ShaderManager& Get();

    void SetContext(IRenderContext* context) { m_Context = context; }

    std::shared_ptr<ShaderHandle> GetOrCreate(
        const std::string& shaderPath,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount);

    // Recompile all shaders when shaderPath empty, otherwise only matching file.
    void Recompile(const std::string& shaderPath = "");
    void RecompileAll() { Recompile(""); }
    void Clear();

private:
    struct ShaderRecord {
        std::string key;
        std::string path;
        std::string vsEntry;
        std::string psEntry;
        const VertexElement* layout = nullptr;
        uint32_t layoutCount = 0;
        std::shared_ptr<ShaderHandle> handle;
    };

    std::shared_ptr<GpuShader> CompileRecord(const ShaderRecord& rec);
    static std::string MakeKey(
        const std::string& shaderPath,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount);

private:
    IRenderContext* m_Context = nullptr;
    std::vector<ShaderRecord> m_Records;
    std::unordered_map<std::string, size_t> m_KeyToIndex;
};
