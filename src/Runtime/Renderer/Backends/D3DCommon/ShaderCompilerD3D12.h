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
    static bool CompileStageFromFile(const std::string& filePath, const std::string& entry, const char* profile,
                                     std::vector<unsigned char>& outBytecode,
                                     const std::vector<std::string>& defines = {});
    // Runtime compile shader file for D3D12 pipeline creation.
    static bool CompileProgramFromFile(const std::string& filePath, const std::string& vsEntry,
                                       const std::string& psEntry, D3D12CompiledShaderProgram& outProgram);
};

#endif
