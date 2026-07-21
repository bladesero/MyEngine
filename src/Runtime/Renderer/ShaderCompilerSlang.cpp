#include "Renderer/ShaderCompilerSlang.h"

#include "Core/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <nlohmann/json.hpp>

#include <SDL3/SDL_filesystem.h>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
std::string Quote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"')
            out += "\\\"";
        else
            out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
#endif
}

std::string SlangcPath() {
    if (const char* env = std::getenv("MYENGINE_SLANGC")) {
        if (*env)
            return env;
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
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Pixel:
        return "fragment";
    case ShaderStage::Compute:
        return "compute";
    }
    return "unknown";
}

const char* TargetName(ShaderBackend backend) {
    switch (backend) {
    case ShaderBackend::D3D11:
        return "dxbc";
    case ShaderBackend::D3D12:
        return "dxil";
    case ShaderBackend::Metal:
        return "metal";
    case ShaderBackend::Vulkan:
        return "spirv";
    }
    return "";
}

const char* ProfileName(ShaderStage stage, ShaderBackend backend) {
    if (backend == ShaderBackend::Metal || backend == ShaderBackend::Vulkan)
        return "";
    if (backend == ShaderBackend::D3D12) {
        return "sm_6_6";
    }
    switch (stage) {
    case ShaderStage::Vertex:
        return "vs_5_0";
    case ShaderStage::Pixel:
        return "ps_5_0";
    case ShaderStage::Compute:
        return "cs_5_0";
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
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0)
        return false;
    input.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return out.empty() || static_cast<bool>(input.read(reinterpret_cast<char*>(out.data()), size));
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

enum class CommandResult { Succeeded, Failed, TimedOut };

CommandResult RunCommand(const std::string& command) {
#ifdef _WIN32
    const auto toWide = [](const std::string& value) {
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        int length = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (length <= 0) {
            codePage = CP_ACP;
            flags = 0;
            length = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        }
        std::wstring converted(static_cast<size_t>((std::max)(length, 0)), L'\0');
        if (length > 0)
            MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), converted.data(),
                                length);
        return converted;
    };
    DWORD timeoutMs = 30000;
    if (const char* timeout = std::getenv("MYENGINE_SLANG_TIMEOUT_MS")) {
        char* end = nullptr;
        const unsigned long requested = std::strtoul(timeout, &end, 10);
        if (end != timeout && *end == '\0')
            timeoutMs = static_cast<DWORD>((std::max)(100ul, (std::min)(requested, 600000ul)));
    }
    std::wstring commandLine = L"cmd.exe /D /S /C \"" + toWide(command) + L"\"";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr, nullptr, &startup, &process)) {
        Logger::Error("[ShaderCompilerSlang] Failed to launch slangc command, Win32 error ", GetLastError());
        return CommandResult::Failed;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    bool assignedToJob = false;
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            AssignProcessToJobObject(job, process.hProcess)) {
            assignedToJob = true;
        } else {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(process.hThread);
    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    DWORD exitCode = ERROR_GEN_FAILURE;
    const bool completed = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode);
    const bool timedOut = waitResult == WAIT_TIMEOUT;
    if (!completed) {
        if (assignedToJob)
            TerminateJobObject(job, ERROR_TIMEOUT);
        else
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(process.hProcess, 5000);
        if (timedOut)
            Logger::Error("[ShaderCompilerSlang] slangc timed out after ", timeoutMs, " ms");
        else
            Logger::Error("[ShaderCompilerSlang] failed waiting for slangc process, Win32 error ", GetLastError());
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job)
        CloseHandle(job);
    if (timedOut)
        return CommandResult::TimedOut;
    return completed && exitCode == 0 ? CommandResult::Succeeded : CommandResult::Failed;
#else
    return std::system(command.c_str()) == 0 ? CommandResult::Succeeded : CommandResult::Failed;
#endif
}

bool ParseReflection(const std::filesystem::path& path, const std::string& entry, CookedShaderStageReflection& output,
                     std::string* error) {
    output = {};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "slangc did not emit reflection JSON";
        return false;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(input);
        const auto entryPoints = root.value("entryPoints", nlohmann::json::array());
        std::unordered_set<std::string> usedBindings;
        bool hasEntryPointUsage = false;
        for (const auto& point : entryPoints) {
            if (point.value("name", std::string{}) != entry)
                continue;
            const auto threads = point.value("threadGroupSize", nlohmann::json::array({1, 1, 1}));
            if (threads.is_array() && threads.size() == 3)
                for (size_t axis = 0; axis < 3; ++axis)
                    output.threadGroupSize[axis] = threads[axis].get<uint32_t>();
            const auto entryBindings = point.value("bindings", nlohmann::json::array());
            hasEntryPointUsage = entryBindings.is_array();
            for (const auto& binding : entryBindings) {
                const auto bindingJson = binding.value("binding", nlohmann::json::object());
                if (bindingJson.value("used", 0u) != 0u)
                    usedBindings.insert(binding.value("name", std::string{}));
            }
            break;
        }
        for (const auto& parameter : root.value("parameters", nlohmann::json::array())) {
            const std::string parameterName = parameter.value("name", std::string{});
            if (hasEntryPointUsage && usedBindings.count(parameterName) == 0)
                continue;
            const auto bindingJson = parameter.value("binding", nlohmann::json::object());
            const std::string kind = bindingJson.value("kind", std::string{});
            CookedShaderBinding binding;
            binding.name = parameterName;
            binding.bindPoint = bindingJson.value("index", 0u);
            binding.bindSpace = bindingJson.value("space", 0u);
            const nlohmann::json* type = parameter.contains("type") ? &parameter["type"] : nullptr;
            while (type && type->value("kind", std::string{}) == "array") {
                const uint32_t elementCount = type->value("elementCount", 1u);
                // Slang reports an unsized descriptor array with elementCount=0. Keep zero reserved for
                // malformed cooked metadata and encode the unbounded array explicitly instead; otherwise
                // the v5 reader rejects every shader that owns the global bindless texture declaration.
                if (elementCount == 0) {
                    binding.bindCount = UINT32_MAX;
                } else if (binding.bindCount != UINT32_MAX) {
                    binding.bindCount =
                        elementCount > UINT32_MAX / binding.bindCount ? UINT32_MAX : binding.bindCount * elementCount;
                }
                type = type->contains("elementType") ? &(*type)["elementType"] : nullptr;
            }
            const std::string shape = type ? type->value("baseShape", std::string{}) : std::string{};
            if (kind == "constantBuffer") {
                binding.type = CookedShaderBindingType::ConstantBuffer;
                if (type && type->contains("elementVarLayout"))
                    binding.byteSize =
                        (*type)["elementVarLayout"].value("binding", nlohmann::json::object()).value("size", 0u);
            } else if (kind == "sampler" || kind == "samplerState") {
                // Slang's reflection JSON currently names HLSL SamplerState/ComparisonSamplerState
                // bindings "samplerState". Older Slang versions used "sampler", so accept both spellings
                // as the same cooked-shader ABI binding. Dropping this entry makes named sampler binding
                // fail and can silently eliminate every alpha-tested draw in passes that require it.
                binding.type = CookedShaderBindingType::Sampler;
            } else if (kind == "shaderResource") {
                if (shape == "accelerationStructure")
                    binding.type = CookedShaderBindingType::AccelerationStructure;
                else
                    binding.type = shape == "structuredBuffer" || shape == "byteAddressBuffer"
                                       ? CookedShaderBindingType::StructuredBuffer
                                       : CookedShaderBindingType::Texture;
            } else if (kind == "rayTracingAccelerationStructure") {
                binding.type = CookedShaderBindingType::AccelerationStructure;
            } else if (kind == "unorderedAccess") {
                binding.type = shape == "structuredBuffer" || shape == "byteAddressBuffer"
                                   ? CookedShaderBindingType::StorageBuffer
                                   : CookedShaderBindingType::StorageTexture;
            } else {
                continue;
            }
            if (!binding.name.empty())
                output.bindings.push_back(std::move(binding));
        }
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error = std::string("invalid slang reflection JSON: ") + exception.what();
        return false;
    }
}
} // namespace

bool ShaderCompilerSlang::IsAvailable() {
    return !GetVersionString().empty();
}

std::string ShaderCompilerSlang::GetVersionString() {
    // The compiler identity is immutable for the lifetime of an Editor/Player process. Cache it here because shader
    // cache-key construction runs once per descriptor and spawning `slangc -version` for every key dominated warm
    // startup once the Modern pipeline grew to several compute shaders. Function-local initialization is thread-safe,
    // which also lets the Editor prewarm independent shader artifacts concurrently.
    static const std::string version = [] {
        const auto diag = TempPath(".txt");
        std::ostringstream command;
        command << Quote(SlangcPath()) << " -version > " << Quote(diag.string()) << " 2>&1";
        if (RunCommand(command.str()) != CommandResult::Succeeded) {
            std::error_code ec;
            std::filesystem::remove(diag, ec);
            return std::string{};
        }
        std::string text = ReadText(diag);
        std::error_code ec;
        std::filesystem::remove(diag, ec);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        return text;
    }();
    return version;
}

bool ShaderCompilerSlang::CompileStageFromFile(const std::filesystem::path& filePath, const std::string& entry,
                                               ShaderStage stage, ShaderBackend backend, std::vector<uint8_t>& outBlob,
                                               const std::vector<std::string>& defines, std::string* error,
                                               CookedShaderStageReflection* reflection) {
    outBlob.clear();
    const char* target = TargetName(backend);
    const char* profile = ProfileName(stage, backend);
    if (!*target) {
        if (error)
            *error = "unsupported Slang shader target";
        return false;
    }

    std::string diagnostics;
    std::string reflectionDiagnostic;
    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        outBlob.clear();
        if (reflection)
            *reflection = {};
        const auto output =
            TempPath(backend == ShaderBackend::Metal ? ".metal" : (backend == ShaderBackend::Vulkan ? ".spv" : ".bin"));
        const auto diag = TempPath(".txt");
        const auto reflectionPath = TempPath(".reflection.json");
        std::ostringstream command;
        command << Quote(SlangcPath()) << " " << Quote(filePath.string()) << " -entry " << Quote(entry) << " -stage "
                << StageName(stage) << " -target " << target;
        command << " -matrix-layout-row-major";
        switch (backend) {
        case ShaderBackend::D3D12:
            command << " -DMYENGINE_D3D12=1";
            break;
        case ShaderBackend::Vulkan:
            command << " -DMYENGINE_VULKAN=1";
            break;
        case ShaderBackend::Metal:
            command << " -DMYENGINE_METAL=1";
            break;
        default:
            break;
        }
        if (reflection)
            command << " -reflection-json " << Quote(reflectionPath.string());
        if (backend == ShaderBackend::Vulkan) {
            command << " -fvk-use-dx-layout"
                    << " -fvk-b-shift 0 0"
                    << " -fvk-t-shift 16 0"
                    << " -fvk-t-shift 0 1"
                    << " -fvk-s-shift 64 0"
                    << " -fvk-u-shift 128 0";
        }
        if (*profile)
            command << " -profile " << profile;
        command << " -o " << Quote(output.string());
        for (const auto& define : defines)
            command << " -D" << Quote(define);
        command << " > " << Quote(diag.string()) << " 2>&1";

        const CommandResult commandResult = RunCommand(command.str());
        const bool compiled =
            commandResult == CommandResult::Succeeded && ReadFile(output, outBlob) && !outBlob.empty();
        reflectionDiagnostic.clear();
        const bool reflected =
            compiled && (!reflection || ParseReflection(reflectionPath, entry, *reflection, &reflectionDiagnostic));
        diagnostics = ReadText(diag);
        std::error_code ec;
        std::filesystem::remove(output, ec);
        std::filesystem::remove(diag, ec);
        std::filesystem::remove(reflectionPath, ec);
        if (compiled && reflected)
            return true;
        if (commandResult == CommandResult::TimedOut) {
            if (error) {
                *error = "slangc timed out for " + filePath.string() + " entry=" + entry + " target=" + target;
            }
            Logger::Error("[ShaderCompileError] ", error ? *error : "slangc timed out");
            return false;
        }
        if (attempt == 0) {
            Logger::Warn("[ShaderCompilerSlang] Retrying transient compiler failure for ", filePath.string(),
                         " entry=", entry);
        }
    }

    if (error) {
        const std::string detail = !diagnostics.empty() ? diagnostics : reflectionDiagnostic;
        *error = "slangc failed for " + filePath.string() + " entry=" + entry + " target=" + target +
                 (detail.empty() ? "" : ": " + detail);
    }
    Logger::Error("[ShaderCompileError] ", error ? *error : "slangc failed");
    return false;
}
