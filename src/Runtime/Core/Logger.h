#pragma once

#include "API/RuntimeApi.h"

#include "Core/Platform.h"
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

class MYENGINE_RUNTIME_API Logger {
public:
    using LogSink = std::function<void(const std::string&)>;

    template <typename... Args> static void Info(Args&&... args) {
        Emit("Info", ComposeMessage(std::forward<Args>(args)...));
    }

    template <typename... Args> static void Warn(Args&&... args) {
        Emit("Warn", ComposeMessage(std::forward<Args>(args)...));
    }

    template <typename... Args> static void Error(Args&&... args) {
        Emit("Error", ComposeMessage(std::forward<Args>(args)...));
    }

    static void SetSink(LogSink sink);

private:
    template <typename... Args> static std::string ComposeMessage(Args&&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        return oss.str();
    }

    static void Emit(const char* level, const std::string& message);
};
