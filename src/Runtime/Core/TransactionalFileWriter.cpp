#include "Core/TransactionalFileWriter.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {
std::atomic<uint64_t> g_Sequence{0};
thread_local TransactionalWriteFault g_NextFault = TransactionalWriteFault::None;
void SetError(std::string* error, const std::string& value) {
    if (error)
        *error = value;
}
bool Inject(TransactionalWriteFault actual, const TransactionalWriteOptions& options, std::string* error) {
    if (options.injectedFault != actual)
        return false;
    SetError(error, "transactional write fault injected at stage " + std::to_string(static_cast<int>(actual)));
    return true;
}
bool FlushFile(FILE* file) {
    if (std::fflush(file) != 0)
        return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}
bool ReplaceFile(const fs::path& temporary, const fs::path& destination, std::string* error) {
#if defined(_WIN32)
    DWORD replaceError = ERROR_SUCCESS;
    for (uint32_t attempt = 0; attempt < 48; ++attempt) {
        if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        replaceError = GetLastError();
        if (replaceError != ERROR_SHARING_VIOLATION && replaceError != ERROR_ACCESS_DENIED)
            break;
        // Virus scanners and another cache reader can briefly hold a just-created artifact. Preserve the atomic
        // replace contract while allowing that transient Windows handle to drain.
        Sleep(attempt < 6 ? (1u << attempt) : 50u);
    }
    SetError(error, "atomic replace failed from '" + temporary.string() + "' to '" + destination.string() +
                        "' with Win32 error " + std::to_string(replaceError));
    return false;
#else
    if (::rename(temporary.c_str(), destination.c_str()) == 0)
        return true;
    SetError(error, "atomic replace failed");
    return false;
#endif
}
} // namespace

void TransactionalFileWriter::SetNextFaultForTesting(TransactionalWriteFault fault) {
    g_NextFault = fault;
}

bool TransactionalFileWriter::WriteText(const fs::path& destination, std::string_view text,
                                        const TransactionalWriteOptions& options, std::string* error) {
    TransactionalWriteOptions effectiveOptions = options;
    if (effectiveOptions.injectedFault == TransactionalWriteFault::None)
        effectiveOptions.injectedFault = g_NextFault;
    g_NextFault = TransactionalWriteFault::None;
    if (error)
        error->clear();
    if (destination.empty()) {
        SetError(error, "transaction destination is empty");
        return false;
    }
    std::error_code ec;
    if (!destination.parent_path().empty())
        fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "failed to create destination directory: " + ec.message());
        return false;
    }
    fs::path temporary = destination;
#if defined(_WIN32)
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId()) +
                 L"." + std::to_wstring(++g_Sequence);
#else
    temporary += ".tmp." + std::to_string(getpid()) + "." +
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "." +
                 std::to_string(++g_Sequence);
#endif
    const fs::path backup = destination.string() + ".bak";
    FILE* file = nullptr;
    int openError = 0;
#if defined(_WIN32)
    openError = _wfopen_s(&file, temporary.c_str(), L"wb");
    if (openError != 0)
        file = nullptr;
#else
    file = std::fopen(temporary.c_str(), "wb");
    if (!file)
        openError = errno;
#endif
    if (!file) {
        SetError(error, "failed to open transaction temporary file '" + temporary.string() +
                            "': " + std::error_code(openError, std::generic_category()).message());
        return false;
    }
    const bool wrote = text.empty() || std::fwrite(text.data(), 1, text.size(), file) == text.size();
    if (!wrote || Inject(TransactionalWriteFault::AfterWrite, effectiveOptions, error)) {
        std::fclose(file);
        fs::remove(temporary, ec);
        if (wrote && error && error->empty())
            SetError(error, "transaction write failed");
        return false;
    }
    if (!FlushFile(file) || Inject(TransactionalWriteFault::AfterFlush, effectiveOptions, error)) {
        std::fclose(file);
        fs::remove(temporary, ec);
        if (error && error->empty())
            SetError(error, "transaction flush failed");
        return false;
    }
    if (std::fclose(file) != 0) {
        fs::remove(temporary, ec);
        SetError(error, "transaction close failed");
        return false;
    }
    if (options.validator && !options.validator(temporary, error)) {
        fs::remove(temporary, ec);
        return false;
    }
    if (Inject(TransactionalWriteFault::AfterValidation, effectiveOptions, error) ||
        Inject(TransactionalWriteFault::BeforeReplace, effectiveOptions, error)) {
        fs::remove(temporary, ec);
        return false;
    }
    if (options.keepBackup && fs::is_regular_file(destination, ec) && !ec) {
        fs::copy_file(destination, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(temporary, ec);
            SetError(error, "failed to create last-known-good backup");
            return false;
        }
    }
    if (!ReplaceFile(temporary, destination, error)) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}
