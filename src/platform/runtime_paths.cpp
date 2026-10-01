#include "runtime_paths.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <stdlib.h>
#else
#include <unistd.h>
#endif

namespace nocturne {
namespace fs = std::filesystem;
namespace {
std::string command_line = "nocturne";
void environment(const char* name, const std::string& value) {
#if defined(_WIN32)
    if (_putenv_s(name, value.c_str())) throw std::runtime_error("Cannot set runtime environment");
#else
    if (setenv(name, value.c_str(), 1)) throw std::runtime_error("Cannot set runtime environment");
#endif
}
bool contains_pods(const fs::path& path) {
    std::error_code ec;
    for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".pod" && it->is_regular_file()) return true;
    }
    return false;
}
fs::path default_user() {
#if defined(_WIN32)
    const char* home = std::getenv("APPDATA");
    if (home && *home) return fs::path(home) / "NocturneRecompKnightStar";
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
#if defined(__APPLE__)
        return fs::path(home) / "Library/Application Support/NocturneRecompKnightStar";
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        return (xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".local/share") / "NocturneRecompKnightStar";
#endif
    }
#endif
    throw std::runtime_error("Cannot determine user directory; supply --user-dir");
}
fs::path default_data(const fs::path& user) {
    if (const char* env = std::getenv("NOCTURNE_DATA_DIR"); env && *env) return env;
    // Remember the selection beside this profile's saves. An explicit user
    // directory must not silently use another profile's remembered assets.
    std::ifstream remembered(user / "data-directory.txt");
    std::string path;
    if (std::getline(remembered, path) && contains_pods(path)) return path;
    auto dir = executable_path().parent_path();
    // Bundled assets are optional. A project build also finds data/original by
    // walking parents, which works after moving the project or Finder launch.
    if (contains_pods(dir / "../Resources/data")) return dir / "../Resources/data";
    for (auto p = dir; !p.empty(); p = p.parent_path()) {
        if (contains_pods(p / "data/original")) return p / "data/original";
        if (p == p.root_path()) break;
    }
    if (contains_pods(fs::current_path())) return fs::current_path();
    throw std::runtime_error("Game data was not found. Launch with --data-dir /path/to/Nocturne");
}
void initial_configuration(const fs::path& data, const fs::path& user) {
    const fs::path ini = user / "system/nocturne.ini";
    if (fs::exists(ini)) return;
    fs::path source;
    for (const auto& d : fs::directory_iterator(data)) {
        std::string name = d.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
        if (name != "system" || !d.is_directory()) continue;
        for (const auto& f : fs::directory_iterator(d.path())) {
            name = f.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
            if (name == "nocturne.ini") source = f.path();
        }
    }
    std::ifstream input(source);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    const auto normalized = [](std::string value) {
        const auto first = value.find_first_not_of(" \t");
        if (first == std::string::npos) return std::string();
        value = value.substr(first, value.find_last_not_of(" \t") - first + 1);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return value;
    };
    // Seed only the new profile. Existing settings remain user-owned. The
    // original first-run calibration disables the renderer, so mark the
    // native profile initialized along with its verified framebuffer defaults.
    const std::array<std::pair<const char*, const char*>, 6> graphics{{
        {"gamePIXX", "640"}, {"gamePIXY", "480"}, {"gameBPP", "32"},
        {"useDirect3D", "1"}, {"rendererDLLPath", "trigl.dll"}, {"firstTimeFlag", "0"}
    }};
    std::ofstream output(ini);
    if (!output) throw std::runtime_error("Cannot create user configuration");
    const auto write_graphics = [&] {
        output << "[Graphics]\n";
        for (const auto& [key, value] : graphics) output << key << '=' << value << '\n';
    };
    if (std::none_of(lines.begin(), lines.end(), [&](const auto& value) {
            return normalized(value) == "[graphics]";
        })) write_graphics();
    bool in_graphics = false;
    for (const auto& line : lines) {
        const auto token = normalized(line);
        if (token == "[graphics]") {
            in_graphics = true;
            write_graphics();
            continue;
        }
        if (token.starts_with('[')) in_graphics = false;
        const auto key = normalized(line.substr(0, line.find('=')));
        if (in_graphics && std::any_of(graphics.begin(), graphics.end(), [&](const auto& setting) {
                return key == normalized(setting.first);
            })) continue;
        output << line << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("Cannot write user configuration");
}
}

fs::path executable_path() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size)) throw std::runtime_error("Cannot locate executable");
    return fs::canonical(buffer.data());
#elif defined(_WIN32)
    return fs::canonical(_pgmptr);
#else
    return fs::canonical("/proc/self/exe");
#endif
}

StartupOptions parse_options(int argc, char** argv) {
    StartupOptions result;
    bool passthrough = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--" && !passthrough) { passthrough = true; continue; }
        if (!passthrough && (arg == "--data-dir" || arg == "--user-dir")) {
            if (++i >= argc || !*argv[i] || std::string(argv[i]).starts_with("--"))
                throw std::runtime_error(arg + " requires a directory");
            (arg == "--data-dir" ? result.data_dir : result.user_dir) = fs::absolute(argv[i]);
        } else if (!passthrough && (arg.starts_with("--data-dir=") || arg.starts_with("--user-dir="))) {
            const auto value = arg.substr(arg.find('=') + 1);
            if (value.empty()) throw std::runtime_error("Directory must not be empty");
            (arg.starts_with("--data-dir=") ? result.data_dir : result.user_dir) = fs::absolute(value);
        } else if (!passthrough && (arg == "--help" || arg == "-h")) result.help = true;
        else if (!passthrough && arg == "--print-paths") result.print_paths = true;
        else if (!passthrough && arg.starts_with("-psn_")) continue;
        else if (!passthrough && arg.starts_with("--")) throw std::runtime_error("Unknown option: " + arg);
        else result.game_args.push_back(arg);
    }
    return result;
}

bool prepare_runtime(const StartupOptions& options) {
    if (options.help) {
        std::cout << "Nocturne Recomp (KnightStar)\n  --data-dir PATH    Original POD/media directory\n"
                     "  --user-dir PATH    Configuration, saves and logs\n"
                     "  --print-paths      Resolve paths without starting the game\n"
                     "  --version          Build identity\n  -- ARGS            Original game options\n";
        return false;
    }
    const auto user = fs::weakly_canonical(options.user_dir.empty() ? default_user() : options.user_dir);
    const auto data = fs::weakly_canonical(options.data_dir.empty() ? default_data(user) : options.data_dir);
    if (!contains_pods(data)) throw std::runtime_error("No POD archives found in " + data.string());
    const auto relative = user.lexically_relative(data);
    if (user == data || (!relative.empty() && *relative.begin() != ".."))
        throw std::runtime_error("User directory must be outside the original game data");
    if (options.print_paths) {
        std::cout << "data-dir=" << data.string() << "\nuser-dir=" << user.string() << '\n';
        return false;
    }
    // Native options never reach the original parser: it splits at whitespace
    // and has fixed buffers. Its argv[0] is a short stable name, even in a path
    // with spaces. Preserve original switches, but reject unrepresentable input.
    command_line = "nocturne";
    // g_ArgVector has 128 slots including argv[0]. The recovered tokenizer
    // consumes every byte <= 0x20 as a delimiter, not only C whitespace.
    if (options.game_args.size() > 127) throw std::runtime_error("Too many game options");
    for (const auto& arg : options.game_args) {
        if (arg.empty() || std::any_of(arg.begin(), arg.end(), [](unsigned char c) { return c <= 0x20; }))
            throw std::runtime_error("Original game options must be nonempty and contain no whitespace or control bytes");
        command_line += " " + arg;
    }
    // It also reads one byte after the final terminator; reserve both zeroes
    // in the 256-byte, zero-initialized g_CommandLineBuffer.
    if (command_line.size() >= 255) throw std::runtime_error("Game command line is too long");
    fs::create_directories(user / "system");
    fs::create_directories(user / "save");
    initial_configuration(data, user);
    std::ofstream remembered(user / "data-directory.txt");
    remembered << data.string() << '\n';
    if (!remembered) throw std::runtime_error("Cannot save selected data directory");
    environment("NOCTURNE_DATA_DIR", data.string());
    environment("NOCTURNE_USER_DIR", user.string());
#if defined(__APPLE__)
    environment("NOCTURNE_RESOURCE_DIR", (executable_path().parent_path() / "../Resources").string());
#else
    environment("NOCTURNE_RESOURCE_DIR", executable_path().parent_path().string());
#endif
    fs::current_path(user);
    return true;
}
const std::string& game_command_line() { return command_line; }
}
