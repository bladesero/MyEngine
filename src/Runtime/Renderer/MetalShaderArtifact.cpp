#include "Renderer/MetalShaderArtifact.h"

#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Core/Sha256.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

#if defined(MYENGINE_PLATFORM_MACOS)
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace MetalShaderArtifact {
namespace {
constexpr std::array<uint8_t, 8> kMagic = {'M', 'Y', 'M', 'T', 'L', '0', '0', '1'};
constexpr uint32_t kHeaderBytes = 36;
constexpr uint32_t kFlagSupportsIndirectCommandBuffers = 1u << 0;
constexpr uint64_t kMaxPayloadBytes = 256ull * 1024ull * 1024ull;
constexpr std::array<const char*, 8> kMetalMaterialSamplerNames = {
    "g_LinearRepeatSampler",       "g_PointRepeatSampler",         "g_LinearClampURepeatVSampler",
    "g_PointClampURepeatVSampler", "g_LinearRepeatUClampVSampler", "g_PointRepeatUClampVSampler",
    "g_LinearClampSampler",        "g_PointClampSampler",
};

void SetError(std::string* error, std::string value) {
    if (error)
        *error = std::move(value);
}

template <typename T> void Append(std::vector<uint8_t>& output, T value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    output.insert(output.end(), bytes, bytes + sizeof(T));
}

template <typename T> bool Read(const uint8_t*& cursor, const uint8_t* end, T& value) {
    if (static_cast<size_t>(end - cursor) < sizeof(T))
        return false;
    std::memcpy(&value, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

uint64_t HashBytes(const void* data, size_t size) {
    constexpr uint64_t offset = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;
    uint64_t hash = offset;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }
    return hash;
}

std::string NormalizeSlangBindingName(std::string name) {
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
        name.pop_back();
    if (!name.empty() && name.back() == '_')
        name.pop_back();
    const std::string prefix = "SLANG_ParameterGroup_";
    if (name.rfind(prefix, 0) == 0)
        name.erase(0, prefix.size());
    const std::string suffix = "_natural";
    const size_t suffixPosition = name.find(suffix);
    if (suffixPosition != std::string::npos)
        name.erase(suffixPosition);
    return name;
}

std::string RewriteMetalConstantBufferLayouts(std::string source) {
    static const std::regex vectorRegex(R"(\b(half|float|int|uint)(2|3)\b)");
    const std::string marker = "struct SLANG_ParameterGroup_";
    size_t search = 0;
    while ((search = source.find(marker, search)) != std::string::npos) {
        const size_t bodyBegin = source.find('{', search + marker.size());
        if (bodyBegin == std::string::npos)
            return {};
        const size_t bodyEnd = source.find("};", bodyBegin + 1);
        if (bodyEnd == std::string::npos)
            return {};
        const std::string body = source.substr(bodyBegin + 1, bodyEnd - bodyBegin - 1);
        const std::string packedBody = std::regex_replace(body, vectorRegex, "packed_$1$2");
        source.replace(bodyBegin + 1, bodyEnd - bodyBegin - 1, packedBody);
        search = bodyBegin + 1 + packedBody.size() + 2;
    }
    return source;
}

std::string RewriteMetalBindlessArgumentBuffer(std::string source) {
    if (source.find("g_BindlessTextures_") == std::string::npos)
        return source;

    constexpr const char* declaration =
        "\nstruct MyEngineBindlessTextureTable\n"
        "{\n"
        "    array<texture2d<float, access::sample>, 4096> textures [[id(0)]];\n"
        "    sampler g_LinearRepeatSampler [[id(4096)]];\n"
        "    sampler g_PointRepeatSampler [[id(4097)]];\n"
        "    sampler g_LinearClampURepeatVSampler [[id(4098)]];\n"
        "    sampler g_PointClampURepeatVSampler [[id(4099)]];\n"
        "    sampler g_LinearRepeatUClampVSampler [[id(4100)]];\n"
        "    sampler g_PointRepeatUClampVSampler [[id(4101)]];\n"
        "    sampler g_LinearClampSampler [[id(4102)]];\n"
        "    sampler g_PointClampSampler [[id(4103)]];\n"
        "};\n";
    const std::string marker = "using namespace metal;";
    const size_t markerPosition = source.find(marker);
    if (markerPosition == std::string::npos)
        return {};
    source.insert(markerPosition + marker.size(), declaration);

    const std::regex memberRegex(
        R"(array<texture2d<float,\s*access::sample>,\s*int\(4096\)>\s+g_BindlessTextures_(\d+);)");
    source = std::regex_replace(source, memberRegex, "constant MyEngineBindlessTextureTable* g_BindlessTable_$1;");
    const std::regex parameterRegex(
        R"(array<texture2d<float,\s*access::sample>,\s*int\(4096\)>\s+g_BindlessTextures_(\d+))");
    source = std::regex_replace(source, parameterRegex,
                                "constant MyEngineBindlessTextureTable& g_BindlessTable_$1 [[buffer(14)]]");
    const std::regex assignmentRegex(R"(->g_BindlessTextures_(\d+)\s*=\s*g_BindlessTextures_(\d+);)");
    source = std::regex_replace(source, assignmentRegex, "->g_BindlessTable_$1 = &g_BindlessTable_$2;");
    const std::regex accessRegex(R"(->g_BindlessTextures_(\d+)\[)");
    source = std::regex_replace(source, accessRegex, "->g_BindlessTable_$1->textures[");

    std::smatch tableMember;
    static const std::regex tableMemberRegex(R"(constant\s+MyEngineBindlessTextureTable\*\s+g_BindlessTable_(\d+);)");
    if (!std::regex_search(source, tableMember, tableMemberRegex))
        return {};
    const std::string tableSuffix = tableMember[1].str();
    for (const char* samplerName : kMetalMaterialSamplerNames) {
        source = std::regex_replace(
            source, std::regex(",\\s*sampler\\s+" + std::string(samplerName) + "_\\d+\\s*\\[\\[sampler\\(\\d+\\)\\]\\]"),
            "");
        source = std::regex_replace(source, std::regex("\\s*sampler\\s+" + std::string(samplerName) + "_\\d+\\s*;"),
                                    "");
        source = std::regex_replace(
            source,
            std::regex("\\s*\\(&[A-Za-z_][A-Za-z0-9_]*\\)->" + std::string(samplerName) +
                       "_\\d+\\s*=\\s*" + std::string(samplerName) + "_\\d+\\s*;"),
            "");
        source = std::regex_replace(source, std::regex("->" + std::string(samplerName) + "_\\d+"),
                                    "->g_BindlessTable_" + tableSuffix + "->" + samplerName);
    }
    return source;
}

void CollectMetalBufferBindingNames(const std::string& source, std::vector<std::string>& names) {
    static const std::regex regex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[buffer\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), regex), end; it != end; ++it) {
        const std::string name = NormalizeSlangBindingName((*it)[2].str());
        if (!name.empty() && name != "g_BindlessTable")
            names.push_back(name);
    }
}

std::string RemapMetalBufferBindings(const std::string& source,
                                     const std::unordered_map<std::string, uint32_t>& bindings) {
    static const std::regex regex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[buffer\((\d+)\)\]\])");
    std::string result;
    size_t cursor = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), regex), end; it != end; ++it) {
        const std::smatch& match = *it;
        result.append(source, cursor, static_cast<size_t>(match.position()) - cursor);
        const std::string rawName = match[2].str();
        const std::string normalized = NormalizeSlangBindingName(rawName);
        const auto found = bindings.find(normalized);
        if (normalized == "g_BindlessTable") {
            result += match.str();
        } else if (found != bindings.end()) {
            result += match[1].str() + " " + rawName + " [[buffer(" + std::to_string(found->second) + ")]]";
        } else {
            result += match.str();
        }
        cursor = static_cast<size_t>(match.position() + match.length());
    }
    result.append(source, cursor, std::string::npos);
    return result;
}

bool RewriteMetalBufferBindings(std::string& first, std::string* second) {
    std::vector<std::string> names;
    CollectMetalBufferBindingNames(first, names);
    if (second)
        CollectMetalBufferBindingNames(*second, names);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    names.erase(std::remove(names.begin(), names.end(), "g_BindlessTable"), names.end());
    if (names.size() > 14)
        return false;
    std::unordered_map<std::string, uint32_t> bindings;
    for (uint32_t index = 0; index < names.size(); ++index)
        bindings[names[index]] = index;
    first = RemapMetalBufferBindings(first, bindings);
    if (second)
        *second = RemapMetalBufferBindings(*second, bindings);
    return true;
}

void CollectMetalTextureBindingNames(const std::string& source, std::vector<std::string>& names) {
    static const std::regex regex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[(texture|sampler)\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), regex), end; it != end; ++it) {
        const std::string name = NormalizeSlangBindingName((*it)[2].str());
        if (!name.empty())
            names.push_back((*it)[3].str() + ":" + name);
    }
}

std::string RemapMetalTextureBindings(const std::string& source,
                                      const std::unordered_map<std::string, uint32_t>& bindings) {
    static const std::regex regex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[(texture|sampler)\((\d+)\)\]\])");
    std::string result;
    size_t cursor = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), regex), end; it != end; ++it) {
        const std::smatch& match = *it;
        result.append(source, cursor, static_cast<size_t>(match.position()) - cursor);
        const std::string rawName = match[2].str();
        const auto found = bindings.find(match[3].str() + ":" + NormalizeSlangBindingName(rawName));
        if (found != bindings.end()) {
            result += match[1].str() + " " + rawName + " [[" + match[3].str() + "(" +
                      std::to_string(found->second) + ")]]";
        } else {
            result += match.str();
        }
        cursor = static_cast<size_t>(match.position() + match.length());
    }
    result.append(source, cursor, std::string::npos);
    return result;
}

bool RewriteMetalTextureBindings(std::string& first, std::string* second) {
    std::vector<std::string> names;
    CollectMetalTextureBindingNames(first, names);
    if (second)
        CollectMetalTextureBindingNames(*second, names);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    const size_t textureCount =
        static_cast<size_t>(std::count_if(names.begin(), names.end(),
                                          [](const std::string& name) { return name.rfind("texture:", 0) == 0; }));
    const size_t samplerCount = names.size() - textureCount;
    if (textureCount > 128 || samplerCount > 16)
        return false;
    std::unordered_map<std::string, uint32_t> bindings;
    uint32_t textureIndex = 0;
    uint32_t samplerIndex = 0;
    for (const std::string& name : names) {
        bindings[name] = name.rfind("texture:", 0) == 0 ? textureIndex++ : samplerIndex++;
    }
    first = RemapMetalTextureBindings(first, bindings);
    if (second)
        *second = RemapMetalTextureBindings(*second, bindings);
    return true;
}

struct NativeBinding {
    std::string name;
    CookedShaderBindingType type = CookedShaderBindingType::Texture;
    uint32_t bindPoint = 0;
};

std::vector<NativeBinding> ParseMetalBindings(const std::string& source) {
    std::vector<NativeBinding> bindings;
    static const std::regex bindingRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[(buffer|texture|sampler)\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), bindingRegex), end; it != end; ++it) {
        NativeBinding binding;
        const std::string declaration = (*it)[0].str();
        const std::string type = (*it)[1].str();
        binding.name = NormalizeSlangBindingName((*it)[2].str());
        const std::string attribute = (*it)[3].str();
        if (binding.name.empty() || binding.name == "g_BindlessTable")
            continue;
        binding.bindPoint = static_cast<uint32_t>(std::stoul((*it)[4].str()));
        if (attribute == "texture") {
            binding.type = declaration.find("access::write") != std::string::npos ||
                                   declaration.find("access::read_write") != std::string::npos
                               ? CookedShaderBindingType::StorageTexture
                               : CookedShaderBindingType::Texture;
        } else if (attribute == "sampler") {
            binding.type = CookedShaderBindingType::Sampler;
        } else {
            binding.type = type.find("const device") != std::string::npos
                               ? CookedShaderBindingType::StructuredBuffer
                               : (type.find("device") != std::string::npos ? CookedShaderBindingType::StorageBuffer
                                                                          : CookedShaderBindingType::ConstantBuffer);
        }
        bindings.push_back(std::move(binding));
    }
    static const std::regex argumentSamplerRegex(R"(sampler\s+(g_[A-Za-z0-9_]+)\s*\[\[id\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), argumentSamplerRegex), end; it != end; ++it) {
        NativeBinding binding;
        binding.name = (*it)[1].str();
        binding.type = CookedShaderBindingType::Sampler;
        binding.bindPoint = static_cast<uint32_t>(std::stoul((*it)[2].str()));
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

void ReconcileReflection(const std::string& source, CookedShaderStageReflection& reflection) {
    const std::vector<NativeBinding> native = ParseMetalBindings(source);
    std::vector<CookedShaderBinding> reconciled;
    reconciled.reserve(reflection.bindings.size());
    for (CookedShaderBinding binding : reflection.bindings) {
        const auto found =
            std::find_if(native.begin(), native.end(), [&](const NativeBinding& candidate) {
                return candidate.name == binding.name;
            });
        if (found != native.end()) {
            binding.bindPoint = found->bindPoint;
            if (binding.type == CookedShaderBindingType::Texture ||
                binding.type == CookedShaderBindingType::StorageTexture) {
                binding.type = found->type;
            }
            binding.byteSize = 0;
            reconciled.push_back(std::move(binding));
        } else if (binding.type == CookedShaderBindingType::Texture && binding.bindCount == UINT32_MAX) {
            // The source-level unsized texture table becomes a buffer(14) argument buffer. It intentionally has no
            // direct texture declaration in final MSL, but remains part of the engine reflection contract.
            binding.bindPoint = 14;
            binding.byteSize = 0;
            reconciled.push_back(std::move(binding));
        }
    }
    reflection.bindings = std::move(reconciled);
}

bool HasDirectTextureOrSamplerBindings(const std::string& source) {
    return source.find("[[texture(") != std::string::npos || source.find("[[sampler(") != std::string::npos;
}

#if defined(MYENGINE_PLATFORM_MACOS)
struct ProcessResult {
    bool launched = false;
    bool succeeded = false;
    bool timedOut = false;
    bool cancelled = false;
    std::string output;
};

std::filesystem::path TemporaryPath(const char* suffix) {
    static std::atomic_uint64_t sequence{0};
    return std::filesystem::temp_directory_path() /
           ("myengine_metal_" + std::to_string(static_cast<uint64_t>(getpid())) + "_" +
            std::to_string(++sequence) + suffix);
}

uint32_t MetalToolTimeoutMs() {
    uint32_t timeoutMs = 60000;
    if (const char* timeout = std::getenv("MYENGINE_METAL_TOOL_TIMEOUT_MS"); timeout && *timeout) {
        char* end = nullptr;
        const unsigned long requested = std::strtoul(timeout, &end, 10);
        if (end != timeout && *end == '\0')
            timeoutMs = static_cast<uint32_t>((std::max)(100ul, (std::min)(requested, 600000ul)));
    }
    return timeoutMs;
}

ProcessResult RunProcess(const std::vector<std::string>& arguments, uint32_t timeoutMs = 60000,
                         const std::function<bool()>& cancellationRequested = {}) {
    ProcessResult result;
    if (arguments.empty())
        return result;
    const std::filesystem::path diagnostics = TemporaryPath(".log");
    const int output = ::open(diagnostics.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (output < 0)
        return result;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, output);

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    pid_t child = 0;
    const int spawnResult =
        posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(output);
    result.launched = spawnResult == 0;
    if (result.launched) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        int status = 0;
        for (;;) {
            const pid_t waitResult = waitpid(child, &status, WNOHANG);
            if (waitResult == child) {
                result.succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                break;
            }
            if (waitResult < 0)
                break;
            if (cancellationRequested && cancellationRequested()) {
                result.cancelled = true;
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                result.timedOut = true;
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
                break;
            }
            usleep(10000);
        }
    }
    std::ifstream input(diagnostics, std::ios::binary);
    if (input) {
        std::ostringstream text;
        text << input.rdbuf();
        result.output = text.str();
    }
    std::error_code ignored;
    std::filesystem::remove(diagnostics, ignored);
    return result;
}

std::string LastAbsolutePath(const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    std::string result;
    while (std::getline(lines, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        const size_t begin = line.find_first_not_of(" \t");
        if (begin != std::string::npos && line[begin] == '/')
            result = line.substr(begin);
    }
    return result;
}

std::vector<std::string> CompilerArguments(const std::string& executable, const char*) {
    return {executable};
}

std::string InstalledMetalToolchainIdentifier() {
    const ProcessResult component =
        RunProcess({"xcodebuild", "-showComponent", "MetalToolchain", "-json"}, 10000);
    if (!component.succeeded)
        return {};
    static const std::regex identifierRegex(
        R"toolchain("toolchainIdentifier"\s*:\s*"([^"]+)")toolchain");
    std::smatch match;
    return std::regex_search(component.output, match, identifierRegex) ? match[1].str() : std::string{};
}

std::string FindXcodeTool(const char* name, const std::string& toolchainIdentifier) {
    std::vector<std::string> arguments;
    if (!toolchainIdentifier.empty())
        arguments.insert(arguments.end(), {"--toolchain", toolchainIdentifier});
    arguments.insert(arguments.end(), {"-sdk", "macosx", "--find", name});
    const ProcessResult found = RunProcess([&] {
        std::vector<std::string> command{"xcrun"};
        command.insert(command.end(), arguments.begin(), arguments.end());
        return command;
    }(), 10000);
    return found.succeeded ? LastAbsolutePath(found.output) : std::string{};
}

std::string FindMountedMetalTool(const char* name) {
    const std::filesystem::path mounts = "/var/run/com.apple.security.cryptexd/mnt";
    std::error_code error;
    for (std::filesystem::directory_iterator it(mounts, error), end; !error && it != end; it.increment(error)) {
        const std::string directory = it->path().filename().string();
        if (directory.rfind("com.apple.MobileAsset.MetalToolchain-", 0) != 0)
            continue;
        const std::filesystem::path candidate =
            it->path() / "Metal.xctoolchain" / "usr" / "bin" / name;
        if (std::filesystem::is_regular_file(candidate, error) || std::filesystem::is_symlink(candidate, error))
            return candidate.string();
        error.clear();
    }
    return {};
}

struct ToolchainInfo {
    bool available = false;
    std::string metal;
    std::string metallib;
    std::string fingerprint = "runtime-msl";
};

std::string ToolFileIdentity(const std::filesystem::path& path) {
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    if (error)
        return {};
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error)
        return {};
    return path.string() + "|" + std::to_string(size) + "|" +
           std::to_string(static_cast<int64_t>(modified.time_since_epoch().count()));
}

ToolchainInfo ProbeToolchain() {
    ToolchainInfo result;
    const char* metalOverride = std::getenv("MYENGINE_METAL_COMPILER");
    const char* metallibOverride = std::getenv("MYENGINE_METALLIB_COMPILER");
    const bool needsXcodeMetal = !(metalOverride && *metalOverride) || !(metallibOverride && *metallibOverride);
    const std::string toolchainIdentifier = needsXcodeMetal ? InstalledMetalToolchainIdentifier() : std::string{};
    if (metalOverride && *metalOverride) {
        result.metal = metalOverride;
    } else {
        if (!toolchainIdentifier.empty())
            result.metal = FindXcodeTool("metal", toolchainIdentifier);
        if (result.metal.empty())
            result.metal = FindMountedMetalTool("metal");
        if (result.metal.empty())
            result.metal = FindXcodeTool("metal", {});
    }
    if (metallibOverride && *metallibOverride) {
        result.metallib = metallibOverride;
    } else {
        if (!toolchainIdentifier.empty())
            result.metallib = FindXcodeTool("metallib", toolchainIdentifier);
        if (result.metallib.empty())
            result.metallib = FindMountedMetalTool("metallib");
        if (result.metallib.empty())
            result.metallib = FindXcodeTool("metallib", {});
    }
    if (result.metal.empty() || result.metallib.empty())
        return result;

    std::vector<std::string> metalVersion = CompilerArguments(result.metal, "metal");
    metalVersion.push_back("--version");
    const ProcessResult metal = RunProcess(metalVersion, 10000);
    const std::string metalIdentity = ToolFileIdentity(result.metal);
    const std::string metallibIdentity = ToolFileIdentity(result.metallib);
    if (!metal.succeeded || metalIdentity.empty() || metallibIdentity.empty())
        return result;

    result.available = true;
    const std::string identity =
        metalIdentity + "|" + metal.output + "|" + metallibIdentity + "|metal3.0|macos14.0";
    Sha256 hash;
    hash.Update(identity.data(), identity.size());
    result.fingerprint = "native-" + Sha256::ToHex(hash.Final());
    return result;
}

ToolchainInfo GetToolchainInfo() {
    const char* metal = std::getenv("MYENGINE_METAL_COMPILER");
    const char* metallib = std::getenv("MYENGINE_METALLIB_COMPILER");
    const std::string key = std::string(metal ? metal : "<xcrun>") + "|" +
                            std::string(metallib ? metallib : "<xcrun>");
    static std::mutex mutex;
    static std::unordered_map<std::string, ToolchainInfo> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;
    ToolchainInfo result = ProbeToolchain();
    cache.emplace(key, result);
    return result;
}

bool CompileMetallib(const std::string& source, std::vector<uint8_t>& output, std::string* error,
                     const std::function<bool()>& cancellationRequested) {
    const ToolchainInfo tools = GetToolchainInfo();
    if (!tools.available)
        return false;
    if (cancellationRequested && cancellationRequested()) {
        SetError(error, "Metal compiler cancelled");
        return false;
    }

    const std::filesystem::path sourcePath = TemporaryPath(".metal");
    const std::filesystem::path airPath = TemporaryPath(".air");
    const std::filesystem::path libraryPath = TemporaryPath(".metallib");
    const std::filesystem::path moduleCachePath = TemporaryPath("_modules");
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(sourcePath, ignored);
        std::filesystem::remove(airPath, ignored);
        std::filesystem::remove(libraryPath, ignored);
        std::filesystem::remove_all(moduleCachePath, ignored);
    };
    std::error_code directoryError;
    std::filesystem::create_directories(moduleCachePath, directoryError);
    if (directoryError) {
        cleanup();
        SetError(error, "failed creating isolated Metal module cache");
        return false;
    }
    {
        std::ofstream file(sourcePath, std::ios::binary | std::ios::trunc);
        file.write(source.data(), static_cast<std::streamsize>(source.size()));
        if (!file) {
            cleanup();
            SetError(error, "failed writing temporary Metal source");
            return false;
        }
    }

    std::vector<std::string> metal = CompilerArguments(tools.metal, "metal");
    metal.insert(metal.end(),
                 {"-std=metal3.0", "-mmacosx-version-min=14.0",
                  "-fmodules-cache-path=" + moduleCachePath.string(), "-c", sourcePath.string(), "-o",
                  airPath.string()});
    const ProcessResult metalResult = RunProcess(metal, MetalToolTimeoutMs(), cancellationRequested);
    if (!metalResult.succeeded) {
        cleanup();
        SetError(error, metalResult.cancelled
                            ? "Metal compiler cancelled"
                            : (metalResult.timedOut ? "Metal compiler timed out"
                                                   : "Metal compiler failed: " + metalResult.output));
        return false;
    }
    std::vector<std::string> metallib = CompilerArguments(tools.metallib, "metallib");
    metallib.insert(metallib.end(), {airPath.string(), "-o", libraryPath.string()});
    const ProcessResult metallibResult = RunProcess(metallib, MetalToolTimeoutMs(), cancellationRequested);
    if (!metallibResult.succeeded) {
        cleanup();
        SetError(error, metallibResult.cancelled
                            ? "metallib cancelled"
                            : (metallibResult.timedOut ? "metallib timed out"
                                                      : "metallib failed: " + metallibResult.output));
        return false;
    }
    std::ifstream library(libraryPath, std::ios::binary);
    if (!library) {
        cleanup();
        SetError(error, "metallib did not produce an output library");
        return false;
    }
    library.seekg(0, std::ios::end);
    const std::streamoff size = library.tellg();
    library.seekg(0, std::ios::beg);
    if (size <= 0 || static_cast<uint64_t>(size) > kMaxPayloadBytes) {
        cleanup();
        SetError(error, "metallib output size is invalid");
        return false;
    }
    output.resize(static_cast<size_t>(size));
    const bool read = static_cast<bool>(
        library.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())));
    cleanup();
    if (!read) {
        SetError(error, "failed reading metallib output");
        output.clear();
        return false;
    }
    return true;
}
#endif
} // namespace

bool IsContainer(const void* data, size_t size) {
    return data && size >= kMagic.size() && std::memcmp(data, kMagic.data(), kMagic.size()) == 0;
}

bool Encode(PayloadKind kind, const std::string& entryPoint, bool supportsIndirectCommandBuffers,
            const std::vector<uint8_t>& payload, std::vector<uint8_t>& output, std::string* error) {
    output.clear();
    if ((kind != PayloadKind::MSLSource && kind != PayloadKind::Metallib) || entryPoint.empty() ||
        entryPoint.size() > UINT32_MAX || payload.empty() || payload.size() > kMaxPayloadBytes) {
        SetError(error, "invalid Metal shader payload");
        return false;
    }
    output.reserve(kHeaderBytes + entryPoint.size() + payload.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    Append<uint32_t>(output, kContainerVersion);
    Append<uint32_t>(output, static_cast<uint32_t>(kind));
    Append<uint32_t>(output, supportsIndirectCommandBuffers ? kFlagSupportsIndirectCommandBuffers : 0u);
    Append<uint32_t>(output, static_cast<uint32_t>(entryPoint.size()));
    Append<uint64_t>(output, static_cast<uint64_t>(payload.size()));
    Append<uint64_t>(output, HashBytes(payload.data(), payload.size()));
    output.insert(output.end(), entryPoint.begin(), entryPoint.end());
    output.insert(output.end(), payload.begin(), payload.end());
    return true;
}

bool Decode(const void* data, size_t size, DecodedPayload& output, std::string* error) {
    output = {};
    if (!IsContainer(data, size)) {
        SetError(error, "Metal shader blob has no supported container header");
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint8_t* cursor = bytes + kMagic.size();
    const uint8_t* end = bytes + size;
    uint32_t version = 0;
    uint32_t kind = 0;
    uint32_t flags = 0;
    uint32_t entryBytes = 0;
    uint64_t payloadBytes = 0;
    uint64_t checksum = 0;
    if (!Read(cursor, end, version) || !Read(cursor, end, kind) || !Read(cursor, end, flags) ||
        !Read(cursor, end, entryBytes) || !Read(cursor, end, payloadBytes) || !Read(cursor, end, checksum) ||
        version != kContainerVersion || (kind != static_cast<uint32_t>(PayloadKind::MSLSource) &&
                                         kind != static_cast<uint32_t>(PayloadKind::Metallib)) ||
        entryBytes == 0 || payloadBytes == 0 || payloadBytes > kMaxPayloadBytes ||
        static_cast<uint64_t>(end - cursor) != static_cast<uint64_t>(entryBytes) + payloadBytes) {
        SetError(error, "Metal shader container is malformed");
        return false;
    }
    output.kind = static_cast<PayloadKind>(kind);
    output.supportsIndirectCommandBuffers = (flags & kFlagSupportsIndirectCommandBuffers) != 0;
    output.entryPoint.assign(reinterpret_cast<const char*>(cursor), entryBytes);
    cursor += entryBytes;
    output.data = cursor;
    output.size = static_cast<size_t>(payloadBytes);
    if (output.entryPoint.find('\0') != std::string::npos || HashBytes(output.data, output.size) != checksum) {
        output = {};
        SetError(error, "Metal shader container checksum or entry point is invalid");
        return false;
    }
    return true;
}

bool TransformComputeSource(std::string& source, CookedShaderStageReflection& reflection,
                            bool& supportsIndirectCommandBuffers, std::string* error) {
    source = RewriteMetalConstantBufferLayouts(std::move(source));
    source = RewriteMetalBindlessArgumentBuffer(std::move(source));
    if (source.empty() || !RewriteMetalBufferBindings(source, nullptr) ||
        !RewriteMetalTextureBindings(source, nullptr)) {
        SetError(error, "failed to transform Metal compute shader ABI");
        return false;
    }
    ReconcileReflection(source, reflection);
    supportsIndirectCommandBuffers = !HasDirectTextureOrSamplerBindings(source);
    return true;
}

bool TransformGraphicsSources(std::string& vertexSource, std::string& fragmentSource,
                              CookedShaderStageReflection& vertexReflection,
                              CookedShaderStageReflection& fragmentReflection,
                              bool& supportsIndirectCommandBuffers, std::string* error) {
    vertexSource = RewriteMetalConstantBufferLayouts(std::move(vertexSource));
    fragmentSource = RewriteMetalConstantBufferLayouts(std::move(fragmentSource));
    vertexSource = RewriteMetalBindlessArgumentBuffer(std::move(vertexSource));
    fragmentSource = RewriteMetalBindlessArgumentBuffer(std::move(fragmentSource));
    if (vertexSource.empty() || fragmentSource.empty() || !RewriteMetalBufferBindings(vertexSource, &fragmentSource) ||
        !RewriteMetalTextureBindings(vertexSource, &fragmentSource)) {
        SetError(error, "failed to transform Metal graphics shader ABI");
        return false;
    }
    ReconcileReflection(vertexSource, vertexReflection);
    ReconcileReflection(fragmentSource, fragmentReflection);
    supportsIndirectCommandBuffers =
        !HasDirectTextureOrSamplerBindings(vertexSource) && !HasDirectTextureOrSamplerBindings(fragmentSource);
    return true;
}

bool BuildCookedPayload(const std::string& transformedSource, const std::string& entryPoint,
                        bool supportsIndirectCommandBuffers, std::vector<uint8_t>& output,
                        bool* usedSourceFallback, std::string* error,
                        const std::function<bool()>& cancellationRequested) {
    if (usedSourceFallback)
        *usedSourceFallback = false;
    if (transformedSource.empty() || entryPoint.empty()) {
        SetError(error, "cannot build an empty Metal shader payload");
        return false;
    }
    std::vector<uint8_t> payload;
#if defined(MYENGINE_PLATFORM_MACOS)
    if (IsNativeCompilerAvailable()) {
        if (!CompileMetallib(transformedSource, payload, error, cancellationRequested))
            return false;
        return Encode(PayloadKind::Metallib, entryPoint, supportsIndirectCommandBuffers, payload, output, error);
    }
#endif
    if (cancellationRequested && cancellationRequested()) {
        SetError(error, "Metal shader cooking cancelled");
        return false;
    }
    static std::atomic_bool warned{false};
    if (!warned.exchange(true)) {
        Logger::Warn("[MetalShaderArtifact] Xcode Metal Toolchain is unavailable; cooked shaders will retain ",
                     "pre-transformed MSL and compile at runtime");
    }
    payload.assign(transformedSource.begin(), transformedSource.end());
    if (usedSourceFallback)
        *usedSourceFallback = true;
    return Encode(PayloadKind::MSLSource, entryPoint, supportsIndirectCommandBuffers, payload, output, error);
}

bool IsNativeCompilerAvailable() {
#if defined(MYENGINE_PLATFORM_MACOS)
    return GetToolchainInfo().available;
#else
    return false;
#endif
}

std::string GetToolchainFingerprint() {
#if defined(MYENGINE_PLATFORM_MACOS)
    return GetToolchainInfo().fingerprint;
#else
    return "runtime-msl-nonmac";
#endif
}

} // namespace MetalShaderArtifact
