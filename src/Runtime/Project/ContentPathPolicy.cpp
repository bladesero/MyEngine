#include "Project/ContentPathPolicy.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace fs = std::filesystem;
namespace {
void SetError(std::string* error, std::string value) { if (error) *error = std::move(value); }
bool Within(const fs::path& child, const fs::path& root) {
    std::error_code ec; const fs::path relative = fs::relative(child, root, ec);
    return !ec && !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
std::string Fold(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
#ifdef _WIN32
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return value;
}
}
bool ContentPathPolicy::ResolveContained(const fs::path& root, const fs::path& candidate,
                                         fs::path& resolved, std::string* error,
                                         bool requireFile) {
    if (error) error->clear(); std::error_code ec;
    if (candidate.is_absolute() || candidate.has_root_name() || candidate.has_root_directory()) {
        SetError(error, "Content path must be relative: " + candidate.string()); return false;
    }
    for (const auto& part : candidate) if (part == ".." || part == ".") {
        SetError(error, "Content path contains a forbidden segment: " + candidate.string()); return false;
    }
    const fs::path canonicalRoot = fs::canonical(root, ec);
    if (ec) { SetError(error, "Content root is unavailable: " + root.string()); return false; }
    resolved = fs::weakly_canonical(canonicalRoot / candidate, ec);
    if (ec || !Within(resolved, canonicalRoot)) {
        SetError(error, "Content path escapes its root: " + candidate.string()); return false;
    }
    if (requireFile && (!fs::is_regular_file(resolved, ec) || ec)) {
        SetError(error, "Content dependency is missing: " + candidate.string()); return false;
    }
    return true;
}
bool ContentPathPolicy::Enumerate(const fs::path& root, std::vector<ContentFileInfo>& files,
                                  uint64_t& totalBytes, std::string* error,
                                  const ContentLimits& limits) {
    if (error) error->clear(); files.clear(); totalBytes = 0; std::error_code ec;
    const fs::path canonicalRoot = fs::canonical(root, ec);
    if (ec || !fs::is_directory(canonicalRoot, ec)) { SetError(error, "Content root is unavailable: " + root.string()); return false; }
    std::unordered_set<std::string> foldedPaths;
    for (fs::recursive_directory_iterator it(canonicalRoot, fs::directory_options::none, ec), end; it != end && !ec; it.increment(ec)) {
        const fs::file_status linkStatus = it->symlink_status(ec);
        if (ec) break;
        if (fs::is_symlink(linkStatus)) { SetError(error, "Content symlinks/junctions are forbidden: " + it->path().string()); return false; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path canonical = fs::canonical(it->path(), ec);
        if (ec || !Within(canonical, canonicalRoot)) { SetError(error, "Content entry escapes its root: " + it->path().string()); return false; }
        const fs::path relative = fs::relative(canonical, canonicalRoot, ec);
        const uint64_t size = fs::file_size(canonical, ec);
        if (ec || relative.empty() || relative.is_absolute()) { SetError(error, "Invalid Content entry: " + it->path().string()); return false; }
        const std::string key = Fold(relative.generic_string());
        if (!foldedPaths.insert(key).second) { SetError(error, "Duplicate/case-colliding Content path: " + relative.generic_string()); return false; }
        if (size > limits.maxFileBytes || files.size() >= limits.maxFiles ||
            size > limits.maxTotalBytes - totalBytes) { SetError(error, "Content limits exceeded at: " + relative.generic_string()); return false; }
        totalBytes += size; files.push_back({canonical, relative, size});
    }
    if (ec) { SetError(error, "Failed to enumerate Content: " + ec.message()); return false; }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b){ return a.relative.generic_string() < b.relative.generic_string(); });
    return true;
}
