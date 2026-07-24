#pragma once

#include "API/RuntimeApi.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

enum class TransactionalWriteFault {
    None,
    AfterWrite,
    AfterFlush,
    AfterValidation,
    BeforeReplace,
};

struct TransactionalWriteOptions {
    bool keepBackup = true;
    TransactionalWriteFault injectedFault = TransactionalWriteFault::None;
    std::function<bool(const std::filesystem::path&, std::string*)> validator;
};

class MYENGINE_RUNTIME_API TransactionalFileWriter {
public:
    static void SetNextFaultForTesting(TransactionalWriteFault fault);
    static bool WriteText(const std::filesystem::path& destination, std::string_view text,
                          const TransactionalWriteOptions& options = {}, std::string* error = nullptr);
};
