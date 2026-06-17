#pragma once

#include <string>

class CrashHandler {
public:
    static void Install(const std::string& applicationName);
    static void Uninstall();
    static std::string WriteDiagnosticReport(const std::string& reason);
    static std::string GetLastCrashReportPath();
};
