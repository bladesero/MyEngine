#pragma once

#if defined(_WIN32)
#    if defined(MYENGINE_RUNTIME_EXPORTS)
#        define MYENGINE_RUNTIME_API __declspec(dllexport)
#    else
#        define MYENGINE_RUNTIME_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define MYENGINE_RUNTIME_API __attribute__((visibility("default")))
#else
#    define MYENGINE_RUNTIME_API
#endif
