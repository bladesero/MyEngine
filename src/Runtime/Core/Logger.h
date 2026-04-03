#pragma once

#include "Core/Platform.h"
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class Logger {
public:
    using LogSink = std::function<void(const std::string&)>;

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

    static void SetSink(LogSink sink) {
        std::lock_guard<std::mutex> lock(SinkMutex());
        Sink() = std::move(sink);
    }

private:
    static std::mutex& SinkMutex() {
        static std::mutex mutex;
        return mutex;
    }

    static LogSink& Sink() {
        static LogSink sink;
        return sink;
    }

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
    static std::string ComposeMessage(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        return oss.str();
    }

    template <typename... Args>
    static void Print(const char* level, Args&&... args) {
        const std::string line =
            "[" + Timestamp() + "][" + std::string(level) + "] " +
            ComposeMessage(std::forward<Args>(args)...);

        std::cout << line << '\n';

        LogSink sinkCopy;
        {
            std::lock_guard<std::mutex> lock(SinkMutex());
            sinkCopy = Sink();
        }
        if (sinkCopy) {
            sinkCopy(line);
        }
    }
};
