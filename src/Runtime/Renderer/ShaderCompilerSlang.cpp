#include "Renderer/ShaderCompilerSlang.h"

#include "Core/Logger.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include <SDL3/SDL_filesystem.h>

namespace {
std::string Quote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

std::string SlangcPath() {
    if (const char* env = std::getenv("MYENGINE_SLANGC")) {
        if (*env) return env;
    }
    const std::string executableName =
#ifdef _WIN32
        "slangc.exe";
#else
        "slangc";
#endif
    if (const char* basePath = SDL_GetBasePath()) {
        const std::filesystem::path local = std::filesystem::path(basePath) / executableName;
        std::error_code ec;
        if (std::filesystem::is_regular_file(local, ec) && !ec) {
            return local.string();
        }
    }
    {
        const std::filesystem::path local = std::filesystem::current_path() / executableName;
        std::error_code ec;
        if (std::filesystem::is_regular_file(local, ec) && !ec) {
            return local.string();
        }
    }
    return executableName;
}

const char* StageName(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return "vertex";
    case ShaderStage::Pixel: return "fragment";
    case ShaderStage::Compute: return "compute";
    }
    return "unknown";
}

const char* TargetName(ShaderBackend backend) {
    switch (backend) {
    case ShaderBackend::D3D11: return "dxbc";
    case ShaderBackend::D3D12: return "dxbc";
    case ShaderBackend::Metal: return "metal";
    }
    return "";
}

const char* ProfileName(ShaderStage stage, ShaderBackend backend) {
    if (backend == ShaderBackend::Metal) return "";
    if (backend == ShaderBackend::D3D12) {
        switch (stage) {
        case ShaderStage::Vertex: return "vs_5_1";
        case ShaderStage::Pixel: return "ps_5_1";
        case ShaderStage::Compute: return "cs_5_1";
        }
    }
    switch (stage) {
    case ShaderStage::Vertex: return "vs_5_0";
    case ShaderStage::Pixel: return "ps_5_0";
    case ShaderStage::Compute: return "cs_5_0";
    }
    return "";
}

std::filesystem::path TempPath(const char* suffix) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    std::ostringstream name;
    name << "myengine_slang_" << now << "_" << tid << suffix;
    return std::filesystem::temp_directory_path() / name.str();
}

bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) return false;
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return out.empty() ||
        static_cast<bool>(input.read(reinterpret_cast<char*>(out.data()), size));
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

bool RunCommand(const std::string& command) {
    return std::system(command.c_str()) == 0;
}
}

bool ShaderCompilerSlang::IsAvailable() {
    const auto diag = TempPath(".txt");
    std::ostringstream command;
    command << Quote(SlangcPath()) << " -version > " << Quote(diag.string()) << " 2>&1";
    const bool ok = RunCommand(command.str());
    std::error_code ec;
    std::filesystem::remove(diag, ec);
    return ok;
}

std::string ShaderCompilerSlang::GetVersionString() {
    const auto diag = TempPath(".txt");
    std::ostringstream command;
    command << Quote(SlangcPath()) << " -version > " << Quote(diag.string()) << " 2>&1";
    if (!RunCommand(command.str())) {
        std::error_code ec;
        std::filesystem::remove(diag, ec);
        return {};
    }
    std::string text = ReadText(diag);
    std::error_code ec;
    std::filesystem::remove(diag, ec);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

bool ShaderCompilerSlang::CompileStageFromFile(
    const std::filesystem::path& filePath, const std::string& entry,
    ShaderStage stage, ShaderBackend backend, std::vector<uint8_t>& outBlob,
    const std::vector<std::string>& defines, std::string* error) {
    outBlob.clear();
    const char* target = TargetName(backend);
    const char* profile = ProfileName(stage, backend);
    if (!*target) {
        if (error) *error = "unsupported Slang shader target";
        return false;
    }

    const auto output = TempPath(backend == ShaderBackend::Metal ? ".metal" : ".bin");
    const auto diag = TempPath(".txt");
    std::ostringstream command;
    command << Quote(SlangcPath())
            << " " << Quote(filePath.string())
            << " -entry " << Quote(entry)
            << " -stage " << StageName(stage)
            << " -target " << target;
    if (*profile) command << " -profile " << profile;
    command << " -o " << Quote(output.string());
    for (const auto& define : defines) command << " -D" << Quote(define);
    command << " > " << Quote(diag.string()) << " 2>&1";

    const bool ok = RunCommand(command.str()) && ReadFile(output, outBlob) && !outBlob.empty();
    const std::string diagnostics = ReadText(diag);
    std::error_code ec;
    std::filesystem::remove(output, ec);
    std::filesystem::remove(diag, ec);
    if (!ok) {
        if (error) {
            *error = "slangc failed for " + filePath.string() + " entry=" + entry +
                " target=" + target + (diagnostics.empty() ? "" : ": " + diagnostics);
        }
        Logger::Error("[ShaderCompileError] ", error ? *error : "slangc failed");
        return false;
    }
    return true;
}
