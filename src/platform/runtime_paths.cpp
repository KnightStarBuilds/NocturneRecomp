#include "runtime_paths.h"
#include "config/shim_config_input.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(NOCTURNE_PS4)
#include "ps4_runtime.h"
#include "filesystem.h"
#include <limits.h>
#endif
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
#if defined(NOCTURNE_PS4)
fs::path resolve_directory(const fs::path& path, bool allow_missing_tail) {
    char resolved[PATH_MAX];
    if (nocturne_ps4_resolve_directory(path.c_str(), resolved, sizeof(resolved),
                                      allow_missing_tail) != 0)
        throw fs::filesystem_error("Cannot resolve runtime directory", path,
                                    std::error_code(errno, std::generic_category()));
    return resolved;
}
#endif
void environment(const char* name, const std::string& value) {
#if defined(_WIN32)
    if (_putenv_s(name, value.c_str())) throw std::runtime_error("Cannot set runtime environment");
#else
    if (setenv(name, value.c_str(), 1)) throw std::runtime_error("Cannot set runtime environment");
#endif
}
double smoke_time(const std::string& value, double minimum, const char* option) {
    char* end = nullptr;
    const double seconds = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end || !std::isfinite(seconds) ||
        seconds < minimum || seconds > 600)
        throw std::runtime_error(std::string(option) + " requires a finite time from " +
                                 std::to_string(static_cast<int>(minimum)) + " to 600 seconds");
    return seconds;
}
void validate_smoke_schedule(const std::string& schedule, bool keys) {
    if (schedule.empty()) return; // An explicit empty schedule clears an inherited one.
    const char* option = keys ? "--smoke-keys" : "--smoke-capture-at";
    std::size_t start = 0;
    unsigned count = 0;
    do {
        const auto end = schedule.find(',', start);
        const auto item = schedule.substr(start, end - start);
        const auto colon = keys ? item.find(':') : std::string::npos;
        smoke_time(item.substr(0, colon), 0, option);
        if (keys && (colon == std::string::npos || colon + 1 == item.size() ||
                     std::any_of(item.begin() + colon + 1, item.end(),
                                 [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
            throw std::runtime_error("--smoke-keys requires time:key-name entries");
        if (++count > (keys ? 128u : 64u))
            throw std::runtime_error(std::string(option) + " has too many entries");
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
}
std::optional<fs::path> resolve_smoke_directory(const StartupOptions& options) {
    if (options.smoke_seconds && (!std::isfinite(*options.smoke_seconds) ||
                                 *options.smoke_seconds < 1 || *options.smoke_seconds > 600))
        throw std::runtime_error("--smoke-seconds requires a finite time from 1 to 600 seconds");
    if (options.smoke_capture_at) validate_smoke_schedule(*options.smoke_capture_at, false);
    if (options.smoke_keys) validate_smoke_schedule(*options.smoke_keys, true);
    if (!options.smoke_dir) return std::nullopt;
    if (!options.smoke_dir->is_absolute())
        throw std::runtime_error("--smoke-dir requires an absolute existing directory");
#if defined(NOCTURNE_PS4)
    return resolve_directory(*options.smoke_dir, false);
#else
    const auto directory = fs::canonical(*options.smoke_dir);
    if (!fs::is_directory(directory))
        throw std::runtime_error("--smoke-dir requires an absolute existing directory");
    return directory;
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
#if defined(NOCTURNE_PS4)
    return ps4::user_data;
#elif defined(_WIN32)
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
#if defined(NOCTURNE_PS4)
    // The package mount is stable and independent of the launch directory.
    (void)user;
    return ps4::original_data;
#else
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
#endif
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
#if defined(NOCTURNE_PS4)
        {"useDirect3D", "0"}, {"rendererDLLPath", ""}, {"firstTimeFlag", "0"}
#else
        {"useDirect3D", "1"}, {"rendererDLLPath", "trigl.dll"}, {"firstTimeFlag", "0"}
#endif
    }};
    std::ofstream output(ini);
    if (!output) throw std::runtime_error("Cannot create user configuration");
#if defined(NOCTURNE_PS4)
    bool configured_hero = false;
    bool checking_graphics = false;
    for (const auto& existing : lines) {
        const auto token = normalized(existing);
        if (token.starts_with('[')) checking_graphics = token == "[graphics]";
        if (checking_graphics && normalized(existing.substr(0, existing.find('='))) == "heronumber")
            configured_hero = true;
    }
#endif
    const auto write_graphics = [&] {
        output << "[Graphics]\n";
        for (const auto& [key, value] : graphics) output << key << '=' << value << '\n';
#if defined(NOCTURNE_PS4)
        // A POD-only installation has no INI. The campaign expects Stranger;
        // the zero-initialized Gabriella selection lacks required animations.
        if (!configured_hero) output << "heroNumber=2\n";
#endif
    };
    if (std::none_of(lines.begin(), lines.end(), [&](const auto& value) {
            return normalized(value) == "[graphics]";
        })) write_graphics();
#if GAMEPAD_ONLY
    const auto write_control = [&] { output << "[Control]\ngameControl=2\n"; };
    if (std::none_of(lines.begin(), lines.end(), [&](const auto& value) {
            return normalized(value) == "[control]";
        })) write_control();
    bool in_control = false;
#endif
    bool in_graphics = false;
    for (const auto& line : lines) {
        const auto token = normalized(line);
#if GAMEPAD_ONLY
        if (token.starts_with('[')) in_control = token == "[control]";
        if (token == "[control]") {
            in_graphics = false;
            write_control();
            continue;
        }
#endif
        if (token == "[graphics]") {
            in_graphics = true;
            write_graphics();
            continue;
        }
        if (token.starts_with('[')) in_graphics = false;
        const auto key = normalized(line.substr(0, line.find('=')));
#if GAMEPAD_ONLY
        if (in_control && key == "gamecontrol") continue;
#endif
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
#if defined(NOCTURNE_PS4)
    return ps4::executable;
#elif defined(__APPLE__)
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
        const auto equals = arg.find('=');
        const auto option = arg.substr(0, equals);
        if (!passthrough && (option == "--smoke-dir" || option == "--smoke-seconds" ||
                             option == "--smoke-capture-at" || option == "--smoke-keys")) {
            std::string value;
            if (equals != std::string::npos) value = arg.substr(equals + 1);
            else {
                if (++i >= argc || std::string(argv[i]).starts_with("--"))
                    throw std::runtime_error(option + " requires a value");
                value = argv[i];
            }
            if (option == "--smoke-dir") {
                if (!fs::path(value).is_absolute())
                    throw std::runtime_error("--smoke-dir requires an absolute existing directory");
                result.smoke_dir = value;
            } else if (option == "--smoke-seconds") {
                result.smoke_seconds = smoke_time(value, 1, "--smoke-seconds");
            } else if (option == "--smoke-capture-at") {
                validate_smoke_schedule(value, false);
                result.smoke_capture_at = value;
            } else {
                validate_smoke_schedule(value, true);
                result.smoke_keys = value;
            }
        } else if (!passthrough && (arg == "--data-dir" || arg == "--user-dir")) {
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
                     "  --smoke-dir PATH   Existing absolute diagnostic output directory\n"
                     "  --smoke-seconds N  Diagnostic duration, 1 to 600 seconds\n"
                     "  --smoke-capture-at TIMES  Capture times, e.g. 1,5,10\n"
                     "  --smoke-keys KEYS  Diagnostic input, e.g. 3:Escape,5:Return\n"
                     "  --version          Build identity\n  -- ARGS            Original game options\n";
        return false;
    }
#if defined(NOCTURNE_PS4)
    const auto user = resolve_directory(options.user_dir.empty() ? default_user() : options.user_dir, true);
    const auto data = resolve_directory(options.data_dir.empty() ? default_data(user) : options.data_dir, false);
#else
    const auto user = fs::weakly_canonical(options.user_dir.empty() ? default_user() : options.user_dir);
    const auto data = fs::weakly_canonical(options.data_dir.empty() ? default_data(user) : options.data_dir);
#endif
    const auto smoke_directory = resolve_smoke_directory(options);
    if (!contains_pods(data)) throw std::runtime_error("No POD archives found in " + data.string());
    // Both paths are absolute and validated before any profile files are made.
    // Compare components so aliases containing '..' cannot evade containment.
    const auto relative = user.lexically_relative(data);
    if (user == data || (!relative.empty() && *relative.begin() != ".."))
        throw std::runtime_error("User directory must be outside the original game data");
    if (options.print_paths) {
        std::cout << "data-dir=" << data.string() << "\nuser-dir=" << user.string() << '\n';
        if (smoke_directory) std::cout << "smoke-dir=" << smoke_directory->string() << '\n';
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
#if defined(NOCTURNE_PS4)
    environment("NOCTURNE_RESOURCE_DIR", ps4::resources);
#elif defined(__APPLE__)
    environment("NOCTURNE_RESOURCE_DIR", (executable_path().parent_path() / "../Resources").string());
#else
    environment("NOCTURNE_RESOURCE_DIR", executable_path().parent_path().string());
#endif
#if defined(NOCTURNE_PS4)
    if (nocturne_ps4_chdir(user.c_str()) != 0)
        throw fs::filesystem_error("Cannot select user directory", user,
                                    std::error_code(errno, std::generic_category()));
#else
    fs::current_path(user);
#endif
    // Native diagnostics bypass the recovered command-line tokenizer. Keep
    // inherited host settings intact unless a corresponding option was given.
    if (smoke_directory) environment("NOCTURNE_SMOKE_DIR", smoke_directory->string());
    if (options.smoke_seconds) environment("NOCTURNE_SMOKE_SECONDS", std::to_string(*options.smoke_seconds));
    if (options.smoke_capture_at) environment("NOCTURNE_SMOKE_CAPTURE_AT", *options.smoke_capture_at);
    if (options.smoke_keys) environment("NOCTURNE_SMOKE_KEYS", *options.smoke_keys);
    return true;
}
const std::string& game_command_line() { return command_line; }
}
