#pragma once

#include "API/RuntimeApi.h"

#include "Assets/ShaderGraph.h"

#include <string>
#include <vector>

struct ShaderGraphCompileRequest {
    const ShaderGraph* graph = nullptr;
    const std::vector<ShaderPropertyDesc>* properties = nullptr;
    ShaderShadingModel shadingModel = ShaderShadingModel::Lit;
    ShaderSurfaceType surfaceType = ShaderSurfaceType::Opaque;
    ShaderPass pass = ShaderPass::GBuffer;
};

struct ShaderGraphCompileResult {
    bool succeeded = false;
    std::string hlsl;
    std::vector<ShaderGraphDiagnostic> diagnostics;
};

namespace ShaderGraphCompiler {
MYENGINE_RUNTIME_API bool Validate(const ShaderGraph& graph, const std::vector<ShaderPropertyDesc>& properties,
                                   std::vector<ShaderGraphDiagnostic>& diagnostics);
MYENGINE_RUNTIME_API ShaderGraphCompileResult Compile(const ShaderGraphCompileRequest& request);
MYENGINE_RUNTIME_API std::string BuildCanonicalKey(const ShaderGraph& graph,
                                                   const std::vector<ShaderPropertyDesc>& properties,
                                                   ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType);
} // namespace ShaderGraphCompiler
