#pragma once

#include <filesystem>
#include <string>
#include <vector>

enum class PublishIssueCode {
    MissingDependency,
    UnsafePath,
    InvalidAsset,
    UnsupportedReference,
    PathCollision,
    ContentLimit,
    RuntimeDependency,
    Compatibility
};
struct PublishIssue {
    PublishIssueCode code = PublishIssueCode::InvalidAsset;
    std::string path;
    std::string referrer;
    std::vector<std::string> dependencyChain;
    std::string message;
};
struct PublishPreflightReport {
    std::vector<PublishIssue> errors;
    std::vector<std::string> visitedAssets;
    uint64_t totalContentBytes = 0;
    bool Passed() const { return errors.empty(); }
    std::string Summary() const;
};

class CookDependencyGraph {
public:
    static bool Validate(const std::filesystem::path& projectRoot, PublishPreflightReport& report);
};
