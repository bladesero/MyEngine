#pragma once

#ifdef MYENGINE_PLATFORM_WINDOWS

#include <string>
#include <vector>

struct D3D12CompiledShaderProgram {
    std::vector<unsigned char> vsBytecode;
    std::vector<unsigned char> psBytecode;
};

class ShaderCompilerD3D12 {
public:
    // Runtime compile shader file for D3D12 pipeline creation.
    static bool CompileProgramFromFile(
        const std::string& filePath,
        const std::string& vsEntry,
        const std::string& psEntry,
        D3D12CompiledShaderProgram& outProgram);
};

#endif

