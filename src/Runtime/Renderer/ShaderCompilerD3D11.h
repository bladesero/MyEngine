#pragma once

#ifdef MYENGINE_PLATFORM_WINDOWS

#include <cstddef>
#include <string>
#include <vector>

struct D3D11CompiledShaderProgram {
    std::vector<unsigned char> vsBytecode;
    std::vector<unsigned char> psBytecode;
};

class ShaderCompilerD3D11 {
public:
    static bool CompileStageFromFile(const std::string& filePath,
        const std::string& entry, const char* profile,
        std::vector<unsigned char>& outBytecode,
        const std::vector<std::string>& defines = {});
    // Compile a shader file using D3DCompileFromFile (+ standard include handler).
    static bool CompileProgramFromFile(
        const std::string& filePath,
        const std::string& vsEntry,
        const std::string& psEntry,
        D3D11CompiledShaderProgram& outProgram);
};

#endif
