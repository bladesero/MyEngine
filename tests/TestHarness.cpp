#include "TestHarness.h"

#include "Core/Memory/MemoryService.h"
#include "Input/Input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

std::filesystem::path gExecutableDirectory;

namespace {
std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct RunOptions {
    std::string moduleFilter;
    std::string testFilter;
    bool listOnly = false;
    bool helpOnly = false;
};

void PrintUsage() {
    std::cout
        << "MyEngineTests usage:\n"
        << "  MyEngineTests.exe [--list] [--module <name>] [--test <name>]\n\n"
        << "Options:\n"
        << "  --help            Show this help text\n"
        << "  --list            List all registered tests as Module::Test\n"
        << "  --module <name>   Run only tests from a module, for example --module Project\n"
        << "  --test <name>     Run one test by its registered name\n\n"
        << "Examples:\n"
        << "  MyEngineTests.exe --list\n"
        << "  MyEngineTests.exe --module Project\n"
        << "  MyEngineTests.exe --module Renderer --test TestHeadlessRendering\n";
}

std::string Lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right) {
    return Lower(left) == Lower(right);
}

bool MatchesFilter(const TestCase& test, const RunOptions& options) {
    if (!options.moduleFilter.empty() &&
        !EqualsIgnoreCase(test.module, options.moduleFilter)) {
        return false;
    }
    if (!options.testFilter.empty() &&
        !EqualsIgnoreCase(test.name, options.testFilter)) {
        return false;
    }
    return true;
}

bool ParseArgs(int argc, char** argv, RunOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h" || argument == "/?") {
            options.helpOnly = true;
        } else if (argument == "--list") {
            options.listOnly = true;
        } else if (argument == "--module") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --module\n";
                PrintUsage();
                return false;
            }
            options.moduleFilter = argv[++index];
        } else if (argument.rfind("--module=", 0) == 0) {
            options.moduleFilter = argument.substr(std::string("--module=").size());
        } else if (argument == "--test") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --test\n";
                PrintUsage();
                return false;
            }
            options.testFilter = argv[++index];
        } else if (argument.rfind("--test=", 0) == 0) {
            options.testFilter = argument.substr(std::string("--test=").size());
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            PrintUsage();
            return false;
        }
    }
    return true;
}
}

bool NearlyEqual(float a, float b, float eps) {
    return std::fabs(a - b) <= eps;
}

bool Check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << '\n';
        return false;
    }
    return true;
}

bool RegisterTest(const char* module, const char* name, bool (*fn)()) {
    Registry().push_back({module, name, fn});
    return true;
}

int RunRegisteredTests(int argc, char** argv) {
    if (argc > 0) {
        gExecutableDirectory = std::filesystem::absolute(argv[0]).parent_path();
    }

    RunOptions options;
    if (!ParseArgs(argc, argv, options)) {
        return 2;
    }

    if (options.helpOnly) {
        PrintUsage();
        return 0;
    }

    const auto& tests = Registry();
    if (options.listOnly) {
        for (const TestCase& test : tests) {
            std::cout << test.module << "::" << test.name << '\n';
        }
        return 0;
    }

    MemoryService::Get().Init();

    int failed = 0;
    int executed = 0;
    for (const TestCase& test : tests) {
        if (!MatchesFilter(test, options)) continue;
        ++executed;
        std::cout << "[RUN ] " << test.module << "::" << test.name << '\n';
        if (!test.fn()) {
            ++failed;
        } else {
            std::cout << "[PASS] " << test.module << "::" << test.name << '\n';
        }
    }

    Input::Shutdown();
    MemoryService::Get().Shutdown();

    if (executed == 0) {
        std::cerr << "No tests matched the requested filters\n";
        return 1;
    }
    if (failed == 0) {
        std::cout << "[PASS] Executed " << executed << " test(s)\n";
        return 0;
    }
    std::cerr << "[FAIL] " << failed << " / " << executed << " test(s) failed\n";
    return 1;
}
