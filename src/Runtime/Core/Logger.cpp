#include "Core/Logger.h"

namespace {

std::mutex& SinkMutex() {
    static std::mutex mutex;
    return mutex;
}

Logger::LogSink& Sink() {
    static Logger::LogSink sink;
    return sink;
}

std::string Timestamp() {
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

} // namespace

void Logger::SetSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(SinkMutex());
    Sink() = std::move(sink);
}

void Logger::Emit(const char* level, const std::string& message) {
    const std::string line = "[" + Timestamp() + "][" + std::string(level) + "] " + message;
    std::cout << line << '\n';

    LogSink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(SinkMutex());
        sinkCopy = Sink();
    }
    if (sinkCopy)
        sinkCopy(line);
}
