#pragma once

// ============================================================================
// Platform.h  –  compile-time platform and compiler detection macros
//
//  Platform macros (mutually exclusive):
//    MYENGINE_PLATFORM_WINDOWS  – Win32/Win64
//    MYENGINE_PLATFORM_MACOS    – macOS (desktop)
//    MYENGINE_PLATFORM_LINUX    – Linux
//
//  Compiler macros (mutually exclusive):
//    MYENGINE_COMPILER_MSVC     – Microsoft Visual C++
//    MYENGINE_COMPILER_CLANG    – Clang (including Apple Clang)
//    MYENGINE_COMPILER_GCC      – GCC
// ============================================================================

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#  ifndef MYENGINE_PLATFORM_WINDOWS
#    define MYENGINE_PLATFORM_WINDOWS
#  endif
#elif defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_MAC && !TARGET_OS_IPHONE
#    ifndef MYENGINE_PLATFORM_MACOS
#      define MYENGINE_PLATFORM_MACOS
#    endif
#  endif
#elif defined(__linux__)
#  ifndef MYENGINE_PLATFORM_LINUX
#    define MYENGINE_PLATFORM_LINUX
#  endif
#endif

// ---------------------------------------------------------------------------
// Compiler detection  (check Clang before MSVC: clang-cl defines both)
// ---------------------------------------------------------------------------
#if defined(__clang__)
#  ifndef MYENGINE_COMPILER_CLANG
#    define MYENGINE_COMPILER_CLANG
#  endif
#elif defined(_MSC_VER)
#  ifndef MYENGINE_COMPILER_MSVC
#    define MYENGINE_COMPILER_MSVC
#  endif
#elif defined(__GNUC__)
#  ifndef MYENGINE_COMPILER_GCC
#    define MYENGINE_COMPILER_GCC
#  endif
#endif
