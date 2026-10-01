// =============================================================================
// FILE SEARCH — implementation
// =============================================================================
//
// See file_search.h.

#include "core/file_search.h"
#include "core/ascii_case.h"
#include "watcom/path.h"
#include <cstdlib>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Wildcard matching without recursion. `star` remembers where the last `*` was
// and `retry` where the name had reached when it was taken, so a `*` that
// swallowed too little can give a character back and carry on. Recursing instead
// costs a frame per `*` and, on a pattern of nothing but stars, as many frames
// as the name is long.
bool matches(const char *pattern, const char *name) {
    const char *star = nullptr;
    const char *retry = nullptr;

    while (*name != '\0') {
        if (*pattern == '?' ||
            nocturne_ascii_lower(*pattern) == nocturne_ascii_lower(*name)) {
            ++pattern;
            ++name;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
        } else if (star != nullptr) {
            // The last `*` took too little. Give it one more character.
            pattern = star + 1;
            name = ++retry;
        } else {
            return false;
        }
    }
    // Trailing stars match nothing, which is still a match.
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

// Splits "art\\*.raw" into "art" and "*.raw". A spec with no separator searches
// the working directory, which is reported as an empty directory so the caller
// can tell the two apart and return bare names.
void split(const std::string &filespec, std::string *directory, std::string *pattern) {
    std::string spec = filespec;
    for (char &c : spec) {
        if (c == '\\') c = '/';
    }
    const size_t separator = spec.rfind('/');
    if (separator == std::string::npos) {
        directory->clear();
        *pattern = spec;
    } else {
        *directory = spec.substr(0, separator);
        *pattern = spec.substr(separator + 1);
    }
}

}  // namespace

bool nocturne_name_matches(const char *pattern, const char *name) {
    if (pattern == nullptr || name == nullptr) return false;
    return matches(pattern, name);
}

std::vector<std::string> nocturne_find_files(const char *filespec) {
    std::vector<std::string> found;
    if (filespec == nullptr) return found;

    std::string directory, pattern;
    split(filespec, &directory, &pattern);

    std::vector<std::filesystem::path> roots;
    roots.emplace_back(watcom_resolve_fs_path(directory.empty() ? "." : directory.c_str(), true));
    const auto relative = std::filesystem::path(directory).lexically_normal();
    const std::string first = relative.empty() ? "" : relative.begin()->string();
    const char* data = std::getenv("NOCTURNE_DATA_DIR");
    if (data && *data && !relative.is_absolute() && first != ".." &&
        !nocturne_ascii_iequals(first.c_str(), "save") &&
        !nocturne_ascii_iequals(first.c_str(), "system")) {
        roots.emplace_back(watcom_resolve_fs_path((std::filesystem::path(data) / relative).string().c_str()));
    }
    for (const auto& where : roots) {
        std::error_code ignored;
        std::filesystem::directory_iterator it(where, ignored), end;
        for (; !ignored && it != end; it.increment(ignored)) {
            const std::string name = it->path().filename().string();
            if (!matches(pattern.c_str(), name.c_str())) continue;
            const std::string result = directory.empty() ? name : directory + "/" + name;
            bool duplicate = false;
            for (const auto& prior : found)
                if (nocturne_ascii_iequals(prior.c_str(), result.c_str())) duplicate = true;
            if (!duplicate) found.push_back(result);
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}
