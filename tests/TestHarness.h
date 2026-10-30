#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct TestCase {
    const char* module = "";
    const char* name = "";
    bool (*fn)() = nullptr;
};

extern std::filesystem::path gExecutableDirectory;

bool NearlyEqual(float a, float b, float eps = 1e-4f);
bool Check(bool cond, const std::string& msg);
// Mark an explicitly unsupported host/toolchain check as skipped, never as a passing test.
bool SkipTest(const std::string& reason);
bool RegisterTest(const char* module, const char* name, bool (*fn)());
int RunRegisteredTests(int argc, char** argv);

#define MYENGINE_REGISTER_TEST(moduleName, testName, fn)                                                               \
    namespace {                                                                                                        \
    const bool fn##_registered = RegisterTest(moduleName, testName, fn);                                               \
    }
