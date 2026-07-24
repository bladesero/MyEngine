#pragma once

#include "API/RuntimeApi.h"

#include <string>

class MYENGINE_RUNTIME_API CrashHandler {
public:
    static void Install(const std::string& applicationName);
    static void Uninstall();
    static std::string WriteDiagnosticReport(const std::string& reason);
    static std::string GetLastCrashReportPath();
};
