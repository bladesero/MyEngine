#pragma once

#include "Core/EngineVersion.h"

#include <string_view>

#define MYENGINE_BUILDINFO_STRINGIZE_DETAIL(x) #x
#define MYENGINE_BUILDINFO_STRINGIZE(x) MYENGINE_BUILDINFO_STRINGIZE_DETAIL(x)

#ifndef MYENGINE_BUILD_ID
#define MYENGINE_BUILD_ID local
#endif
#ifndef MYENGINE_BUILD_CONFIGURATION
#define MYENGINE_BUILD_CONFIGURATION unknown
#endif
#ifndef MYENGINE_GIT_COMMIT
#define MYENGINE_GIT_COMMIT unknown
#endif

namespace BuildInfo {
inline constexpr std::string_view EngineVersion = ::EngineVersion::String;
inline constexpr std::string_view BuildId = MYENGINE_BUILDINFO_STRINGIZE(MYENGINE_BUILD_ID);
inline constexpr std::string_view Configuration = MYENGINE_BUILDINFO_STRINGIZE(MYENGINE_BUILD_CONFIGURATION);
inline constexpr std::string_view GitCommit = MYENGINE_BUILDINFO_STRINGIZE(MYENGINE_GIT_COMMIT);
#if defined(_MSC_FULL_VER)
inline constexpr std::string_view Compiler = "MSVC " MYENGINE_BUILDINFO_STRINGIZE(_MSC_FULL_VER);
#elif defined(__clang_version__)
inline constexpr std::string_view Compiler = "Clang " __clang_version__;
#elif defined(__GNUC__)
inline constexpr std::string_view Compiler = "GCC " __VERSION__;
#else
inline constexpr std::string_view Compiler = "unknown";
#endif
#if defined(MYENGINE_PLATFORM_WINDOWS)
inline constexpr std::string_view ShaderTool = "D3DCompile/WindowsSDK";
#elif defined(MYENGINE_PLATFORM_MACOS)
inline constexpr std::string_view ShaderTool = "Slang/Metal";
#else
inline constexpr std::string_view ShaderTool = "unknown";
#endif
} // namespace BuildInfo
