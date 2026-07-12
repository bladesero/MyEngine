#include "Core/CrashHandler.h"

#include "Core/Logger.h"
#include "Core/BuildInfo.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <ctime>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace {

std::string g_ApplicationName = "MyEngine";
std::string g_LastReportPath;
std::terminate_handler g_PreviousTerminate = nullptr;
std::mutex g_ReportMutex;

std::string BuildReportPath()
{
    namespace fs = std::filesystem;
    fs::create_directories("logs");
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef MYENGINE_PLATFORM_WINDOWS
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream name;
    name << "crash-" << std::put_time(&tm, "%Y%m%d-%H%M%S") << ".log";
    return (fs::path("logs") / name.str()).string();
}

void WriteReport(const std::string& reason, const void* address = nullptr)
{
    std::lock_guard<std::mutex> lock(g_ReportMutex);
    g_LastReportPath = BuildReportPath();
    std::ofstream output(g_LastReportPath, std::ios::out | std::ios::trunc);
    output << "application=" << g_ApplicationName << '\n';
    output << "engine_version=" << BuildInfo::EngineVersion << '\n';
    output << "build_id=" << BuildInfo::BuildId << '\n';
    output << "git_commit=" << BuildInfo::GitCommit << '\n';
    output << "configuration=" << BuildInfo::Configuration << '\n';
    output << "compiler=" << BuildInfo::Compiler << '\n';
    output << "shader_tool=" << BuildInfo::ShaderTool << '\n';
    output << "reason=" << reason << '\n';
    output << "thread=" << std::this_thread::get_id() << '\n';
    if (address) output << "address=" << address << '\n';
#ifdef MYENGINE_PLATFORM_WINDOWS
    void* frames[64] = {};
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    output << "stack_frames=" << count << '\n';
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address64 = reinterpret_cast<DWORD64>(frames[i]);
        output << i << '=' << frames[i];
        DWORD64 displacement = 0;
        if (SymFromAddr(process, address64, &displacement, symbol)) {
            output << ' ' << symbol->Name << "+0x" << std::hex << displacement << std::dec;
        }
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address64, &lineDisplacement, &line)) {
            output << " (" << line.FileName << ':' << line.LineNumber << ')';
        }
        output << '\n';
    }

    HMODULE modules[256] = {};
    DWORD needed = 0;
    if (EnumProcessModules(process, modules, sizeof(modules), &needed)) {
        output << "modules=" << (needed / sizeof(HMODULE)) << '\n';
        const DWORD moduleCount = (std::min)(needed / static_cast<DWORD>(sizeof(HMODULE)),
                                             static_cast<DWORD>(256));
        for (DWORD i = 0; i < moduleCount; ++i) {
            MODULEINFO info = {};
            char moduleName[MAX_PATH] = {};
            GetModuleInformation(process, modules[i], &info, sizeof(info));
            GetModuleFileNameA(modules[i], moduleName, MAX_PATH);
            output << "module=" << modules[i]
                   << " size=0x" << std::hex << info.SizeOfImage << std::dec
                   << " path=" << moduleName << '\n';
        }
    }
    SymCleanup(process);
#endif
    output.flush();
}

void OnTerminate()
{
    std::string reason = "std::terminate";
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            reason += std::string(": ") + e.what();
        } catch (...) {
            reason += ": non-standard exception";
        }
    }
    WriteReport(reason);
    std::abort();
}

#ifdef MYENGINE_PLATFORM_WINDOWS
LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
{
    std::ostringstream reason;
    reason << "unhandled SEH code=0x" << std::hex
           << (info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0);
    const void* address =
        info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    WriteReport(reason.str(), address);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

void CrashHandler::Install(const std::string& applicationName)
{
    g_ApplicationName = applicationName.empty() ? "MyEngine" : applicationName;
    g_PreviousTerminate = std::set_terminate(OnTerminate);
#ifdef MYENGINE_PLATFORM_WINDOWS
    SetUnhandledExceptionFilter(OnUnhandledException);
#endif
    Logger::Info("[CrashHandler] Installed; reports directory: logs");
}

void CrashHandler::Uninstall()
{
    if (g_PreviousTerminate) std::set_terminate(g_PreviousTerminate);
    g_PreviousTerminate = nullptr;
#ifdef MYENGINE_PLATFORM_WINDOWS
    SetUnhandledExceptionFilter(nullptr);
#endif
}

std::string CrashHandler::WriteDiagnosticReport(const std::string& reason)
{
    WriteReport("diagnostic: " + reason);
    return GetLastCrashReportPath();
}

std::string CrashHandler::GetLastCrashReportPath()
{
    std::lock_guard<std::mutex> lock(g_ReportMutex);
    return g_LastReportPath;
}
