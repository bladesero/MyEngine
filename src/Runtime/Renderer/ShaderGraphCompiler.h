#pragma once

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
bool Validate(const ShaderGraph& graph, const std::vector<ShaderPropertyDesc>& properties,
              std::vector<ShaderGraphDiagnostic>& diagnostics);
ShaderGraphCompileResult Compile(const ShaderGraphCompileRequest& request);
std::string BuildCanonicalKey(const ShaderGraph& graph, const std::vector<ShaderPropertyDesc>& properties,
                              ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType);
} // namespace ShaderGraphCompiler
