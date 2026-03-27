#pragma once

#include "Core/Platform.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

class Logger {
public:
    template <typename... Args>
    static void Info(Args&&... args) {
        Print("Info", std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warn(Args&&... args) {
        Print("Warn", std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(Args&&... args) {
        Print("Error", std::forward<Args>(args)...);
    }

private:
    static std::string Timestamp() {
        using clock = std::chrono::system_clock;
        const auto now = clock::now();
        const auto tt = clock::to_time_t(now);
        std::tm tm{};
#ifdef MYENGINE_PLATFORM_WINDOWS
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    template <typename... Args>
    static void Print(const char* level, Args&&... args) {
        std::cout << "[" << Timestamp() << "][" << level << "] ";
        (std::cout << ... << args) << '\n';
    }
};
