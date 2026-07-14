#pragma once

#include <filesystem>
#include <string>

struct CookedProjectMount {
    std::filesystem::path projectRoot;
    bool rebuilt = false;
};

class CookedProjectCache {
public:
    static std::filesystem::path DefaultRoot();

    static bool Prepare(const std::filesystem::path& packageRoot, CookedProjectMount& mount,
                        std::string* error = nullptr);
    static bool Prepare(const std::filesystem::path& packageRoot, const std::filesystem::path& cacheBase,
                        CookedProjectMount& mount, std::string* error = nullptr);
};
