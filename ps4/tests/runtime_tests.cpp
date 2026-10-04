#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "runtime_paths.h"
#include "text_stream.h"
#include "ps4_runtime.h"
#include "ime_text.h"
#include "path_resolver.h"
#include "filesystem.h"
#include "game/resolution.h"
#include "renderer/builtin_dll.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
static int denied_chdir_error;
static unsigned chdir_calls;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
static nocturne::StartupOptions parse(std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), "nocturne");
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    return nocturne::parse_options(static_cast<int>(argv.size()), argv.data());
}

int main() {
    fs::path temporary;
    const auto original_cwd = fs::current_path();
    try {
        std::string pattern = (fs::temp_directory_path() / "Nocturne PS4 tests.XXXXXX").string();
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        check(mkdtemp(name.data()) != nullptr, "temporary directory creation");
        // macOS /var and /tmp aliases are links, which console paths reject.
        temporary = fs::canonical(name.data());
        const auto data = temporary / "original data";
        const auto user = temporary / "new profiles/user profile";
        fs::create_directories(data / "SYSTEM");
        std::ofstream(data / "GAME.POD") << "archive fixture";
        const auto source_ini = data / "SYSTEM/NOCTURNE.INI";
        std::ofstream(source_ini) << "[Graphics]\r\ngamePIXX=1920\r\nuseDirect3D=1\r\n"
                                     "rendererDLLPath=trigl.dll\r\n[Sound]\r\nvolume=73\r\n"
                                     "[Control]\r\ngameControl=1\r\nkeyFire=777\r\n";
        const auto original_ini = read(source_ini);
        const auto physical_temporary = fs::canonical(temporary).string();
        char resolved[nocturne::ps4::path_capacity];
        const auto resolve = [&](const char* path) {
            return nocturne::ps4::resolve_path(path, physical_temporary.c_str(),
                                               resolved, sizeof(resolved), lstat);
        };
        check(resolve("./original data/SYSTEM/../GAME.POD") == 0 &&
              fs::path(resolved) == fs::canonical(data / "GAME.POD"),
              "console canonicalization validates and normalizes path components");
        check(resolve("original data/missing/../GAME.POD") == -1 && errno == ENOENT,
              "missing ancestors cannot be hidden by dot-dot");
        check(resolve("original data/GAME.POD/../SYSTEM") == -1 && errno == ENOTDIR,
              "file ancestors cannot be hidden by dot-dot");
        check(resolve("original data/GAME.POD/.") == -1 && errno == ENOTDIR &&
              resolve("original data/GAME.POD/") == -1 && errno == ENOTDIR,
              "dot and trailing separators require a directory");
        fs::create_directory_symlink(data, temporary / "asset link");
        check(resolve("asset link/GAME.POD") == -1 && errno == ELOOP &&
              resolve("asset link/../original data") == -1 && errno == ELOOP,
              "console profiles cannot redirect paths through symbolic links");
        check(resolve("/.././") == 0 && std::strcmp(resolved, "/") == 0,
              "parent traversal stays at the filesystem root");
        const std::string oversized(nocturne::ps4::path_capacity, 'x');
        check(resolve(oversized.c_str()) == -1 && errno == ENAMETOOLONG &&
              resolve("") == -1 && errno == ENOENT &&
              resolve(nullptr) == -1 && errno == EINVAL, "canonical path argument errors");
        check(nocturne::ps4::resolve_path("original data", physical_temporary.c_str(),
                                          resolved, 2, lstat) == -1 && errno == ENAMETOOLONG,
              "canonical path output bounds");
        char cwd_buffer[8] = "retain";
        check(nocturne::ps4::copy_cwd("/app0", cwd_buffer, 5) == nullptr && errno == ERANGE &&
              std::strcmp(cwd_buffer, "retain") == 0, "getcwd does not truncate");
        check(nocturne::ps4::copy_cwd("/app0", cwd_buffer, 0) == nullptr && errno == EINVAL,
              "getcwd caller buffer needs positive capacity");
        char* allocated_cwd = nocturne::ps4::copy_cwd("/app0", nullptr, 0);
        check(allocated_cwd && std::strcmp(allocated_cwd, "/app0") == 0,
              "getcwd supports caller-owned allocated results");
        std::free(allocated_cwd);
        check(nocturne::executable_path() == "/app0/eboot.bin", "PS4 executable mount");

        nocturne::StartupOptions options;
        options.data_dir = data;
        options.print_paths = true;
        std::ostringstream paths;
        auto* previous = std::cout.rdbuf(paths.rdbuf());
        bool informational = !nocturne::prepare_runtime(options);
        std::cout.rdbuf(previous);
        check(informational, "path query is informational");
        check(paths.str().find("user-dir=/data/NocturneRecompKnightStar\n") != std::string::npos,
              "console user directory does not depend on HOME");

        options.print_paths = false;
        const auto reject_user = [&](const fs::path& candidate, int expected_error, const char* message) {
            options.user_dir = candidate;
            const auto cwd = fs::current_path();
            bool rejected = false;
            try { nocturne::prepare_runtime(options); }
            catch (const fs::filesystem_error& error) {
                rejected = expected_error == 0 || error.code().value() == expected_error;
            }
            catch (const std::runtime_error&) { rejected = expected_error == 0; }
            check(rejected && fs::current_path() == cwd, message);
        };
        reject_user(data, 0, "user directory cannot equal installed assets");
        reject_user(data / "../original data/profile", 0,
                    "normalized dot-dot cannot hide an inside-assets user directory");
        reject_user(data / "SYSTEM/../profile", 0,
                    "existing dot-dot still detects containment before creation");
        reject_user(temporary / "missing/../outside profile", ENOENT,
                    "startup rejects missing ancestors before lexical normalization");
        reject_user(data / "GAME.POD/../profile", ENOTDIR,
                    "startup rejects file ancestors before lexical normalization");
        reject_user(temporary / "asset link/../outside profile", ELOOP,
                    "startup rejects linked ancestors before lexical normalization");
        check(!fs::exists(data / "profile") && !fs::exists(temporary / "missing") &&
              !fs::exists(temporary / "outside profile") && !fs::exists(data / "save") &&
              !fs::exists(data / "data-directory.txt") &&
              read(source_ini) == original_ini,
              "rejected paths create no profile files and preserve original settings");

        // Test direct callers with relative paths as well as command-line
        // options, which parse_options already makes absolute.
        fs::current_path(temporary);
        options.data_dir = "original data/SYSTEM/..";
        options.user_dir = "original data/SYSTEM/../../new profiles/./user profile/";
        options.print_paths = true;
        paths.str("");
        previous = std::cout.rdbuf(paths.rdbuf());
        informational = !nocturne::prepare_runtime(options);
        std::cout.rdbuf(previous);
        check(informational && paths.str().find("data-dir=" + data.string() + "\n") != std::string::npos &&
              paths.str().find("user-dir=" + user.string() + "\n") != std::string::npos &&
              !fs::exists(user.parent_path()) && fs::current_path() == temporary,
              "path queries normalize planned nested profiles without creating them or changing cwd");
        options.print_paths = false;
        check(nocturne::prepare_runtime(options), "normalized relative runtime preparation");
        check(fs::equivalent(fs::current_path(), user) &&
              std::string(std::getenv("NOCTURNE_USER_DIR")) == user.string() &&
              std::string(std::getenv("NOCTURNE_DATA_DIR")) == data.string() &&
              read(user / "data-directory.txt") == data.string() + "\n",
              "created files, environment and cwd use the same normalized absolute directories");

        options.data_dir = data;
        options.user_dir = user;
        check(nocturne::prepare_runtime(options), "runtime preparation");
        check(fs::equivalent(fs::current_path(), user), "writable working directory");
        check(std::string(std::getenv("NOCTURNE_RESOURCE_DIR")) == "/app0", "packaged resources");
        check(fs::is_directory(user / "save"), "save directory");
        const auto ini = read(user / "system/nocturne.ini");
        check(ini.find("gamePIXX=640\n") != std::string::npos, "fixed software width");
        check(ini.find("gamePIXY=480\n") != std::string::npos, "fixed software height");
        check(ini.find("gameBPP=32\n") != std::string::npos, "fixed pixel format");
        check(ini.find("useDirect3D=0\n") != std::string::npos, "software profile");
        check(ini.find("rendererDLLPath=\n") != std::string::npos, "no unavailable renderer");
        check(ini.find("heroNumber=2\n") != std::string::npos,
              "missing hero selection starts the campaign with Stranger");
        check(ini.find("volume=73\n") != std::string::npos, "unrelated preferences retained");
#if GAMEPAD_ONLY
        check(ini.find("gameControl=2\n") != std::string::npos &&
              ini.find("gameControl=1\n") == std::string::npos,
              "new gamepad-only profile constrains imported control selection");
#else
        check(ini.find("gameControl=1\n") != std::string::npos,
              "ordinary profile preserves imported control selection");
#endif
        check(ini.find("keyFire=777\n") != std::string::npos,
              "profile seeding preserves custom bindings");
        check(read(source_ini) == original_ini, "original data is immutable");
        std::ofstream(user / "system/nocturne.ini", std::ios::app) << "custom=keep\n";
        check(nocturne::prepare_runtime(options), "existing profile preparation");
        check(read(user / "system/nocturne.ini").find("custom=keep\n") != std::string::npos,
              "existing preferences are not reseeded");

        int width = 1920, height = 1080, bpp = 16, acceleration = 1;
        char renderer[] = "trigl.dll";
        nocturne::ps4::apply_graphics(width, height, bpp, acceleration, renderer);
        check(width == 640 && height == 480 && bpp == 32 && acceleration == 0 && !renderer[0],
              "imported desktop graphics settings are constrained");
        check(nocturne_resolution_count() == 1, "one console resolution");
        check(nocturne_resolution_step(1920, 1080, 1, 0, &width, &height) != 0 &&
                  width == 640 && height == 480, "resolution cycle cannot leave supported mode");
        check(nocturne_builtin_dll_available("trigl.dll") == 0 &&
                  nocturne_builtin_dll_next(nullptr) == nullptr &&
                  nocturne_builtin_dll_open("trigl.dll") == nullptr,
              "software build never advertises GL stubs");

        check(nocturne::ps4::ime_input_capacity(400) == 299 &&
              nocturne::ps4::ime_input_capacity(20) == 20 &&
              nocturne::ps4::ime_input_capacity(0) == 0, "recovered text length bounds");
        const std::uint16_t ascii[]{'S', 'a', 'v', 'e', ' ', '1', 0};
        const std::uint16_t unicode[]{'S', 0x00e9, 0};
        const std::uint16_t surrogate[]{0xd83d, 0xde00, 0};
        const std::uint16_t unfinished[]{'a', 'b'};
        char typed[32] = "unchanged";
        check(!nocturne::ps4::ime_decode_ascii(unicode, 3, typed, sizeof(typed)) &&
              !nocturne::ps4::ime_decode_ascii(surrogate, 3, typed, sizeof(typed)) &&
              !nocturne::ps4::ime_decode_ascii(unfinished, 2, typed, sizeof(typed)) &&
              !nocturne::ps4::ime_decode_ascii(ascii, 7, typed, 6) &&
              std::strcmp(typed, "unchanged") == 0,
              "unsupported or oversized IME text never mutates the caller buffer");
        check(nocturne::ps4::ime_decode_ascii(ascii, 7, typed, 7) &&
              std::strcmp(typed, "Save 1") == 0, "UTF-16 ASCII input and NUL capacity");
        std::strcpy(typed, "save01");
        check(nocturne::ps4::ime_complete_filename(typed, sizeof(typed), "sav") &&
              std::strcmp(typed, "SAVE01.sav") == 0, "uppercase filename and automatic extension");
        std::strcpy(typed, "named.sav");
        check(nocturne::ps4::ime_complete_filename(typed, sizeof(typed), ".sav") &&
              std::strcmp(typed, "NAMED.SAV") == 0, "existing filename extension is retained");
        for (const char* invalid : {"", ".", "..", "../save", "a\\b", "a:b", "a b"}) {
            std::strcpy(typed, invalid);
            check(!nocturne::ps4::ime_complete_filename(typed, sizeof(typed), "sav"),
                  "filename prompt rejects paths and whitespace");
        }
        std::strcpy(typed, "save");
        check(!nocturne::ps4::ime_complete_filename(typed, 8, "sav") &&
              std::strcmp(typed, "save") == 0, "filename extension respects output capacity");

        FILE* text = nocturne_fopen_text(source_ini.c_str(), "rt");
        check(text != nullptr, "CRLF stream opens");
        char line[64];
        check(std::fgets(line, sizeof(line), text) && std::strcmp(line, "[Graphics]\n") == 0,
              "CRLF translation");
        check(std::ftell(text) == 12, "text positions retain physical offsets");
        check(std::fseek(text, 12, SEEK_SET) == 0 && std::fgetc(text) == 'g', "physical seek");
        std::fclose(text);

        const auto selected_data = temporary / "selected hero data";
        fs::create_directories(selected_data / "system");
        std::ofstream(selected_data / "game.pod") << "archive fixture";
        std::ofstream(selected_data / "system/nocturne.ini") << "[Graphics]\nheroNumber=1\n";
        options.data_dir = selected_data;
        options.user_dir = temporary / "selected hero user";
        check(nocturne::prepare_runtime(options), "selected hero profile preparation");
        const auto selected_ini = read(options.user_dir / "system/nocturne.ini");
        check(selected_ini.find("heroNumber=1\n") != std::string::npos &&
              selected_ini.find("heroNumber=2\n") == std::string::npos,
              "an existing hero selection is preserved");

        options.data_dir = data;
        options.user_dir = data / "save";
        bool rejected = false;
        try { nocturne::prepare_runtime(options); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected && !fs::exists(data / "save"), "reject writes inside installed data");

        options.user_dir = temporary / "uncreated profile";
        options.data_dir = data / "missing/..";
        rejected = false;
        try { nocturne::prepare_runtime(options); }
        catch (const fs::filesystem_error& error) { rejected = error.code().value() == ENOENT; }
        check(rejected && !fs::exists(options.user_dir) && !fs::exists(data / "missing"),
              "asset resolution also validates missing ancestors before profile creation");

        options.data_dir = data;
        options.user_dir = temporary / "original data sibling/profile";
        check(nocturne::prepare_runtime(options) && fs::is_directory(options.user_dir / "save"),
              "a sibling with the asset directory's string prefix is outside the assets");
        const auto before_denied_chdir = fs::current_path();
        const auto prior_chdir_calls = chdir_calls;
        options.user_dir = user;
        denied_chdir_error = EACCES;
        rejected = false;
        try { nocturne::prepare_runtime(options); }
        catch (const fs::filesystem_error& error) { rejected = error.code().value() == EACCES; }
        denied_chdir_error = 0;
        check(rejected && chdir_calls == prior_chdir_calls + 1 &&
              fs::current_path() == before_denied_chdir,
              "failed console chdir makes preparation fail and preserves the working directory");

        const auto captures = temporary / "diagnostic output";
        fs::create_directory(captures);
        auto diagnostic = parse({"--smoke-dir", (captures / ".").string(), "--smoke-seconds=25.5",
                                 "--smoke-capture-at", "1,5,10", "--smoke-keys=3:Escape", "-window"});
        check(diagnostic.smoke_dir == captures / "." && diagnostic.smoke_seconds == 25.5 &&
              diagnostic.smoke_capture_at == "1,5,10" && diagnostic.smoke_keys == "3:Escape" &&
              diagnostic.game_args == std::vector<std::string>{"-window"},
              "diagnostic options accept both forms without entering recovered game arguments");
        const auto alternate = parse({"--smoke-dir=" + captures.string(), "--smoke-seconds", "600",
                                      "--smoke-capture-at=0,600", "--smoke-keys", "0:Return,600:Escape"});
        check(alternate.smoke_dir == captures && alternate.smoke_seconds == 600 &&
              alternate.smoke_capture_at == "0,600" && alternate.smoke_keys == "0:Return,600:Escape" &&
              alternate.game_args.empty() && parse({"--smoke-seconds=1"}).smoke_seconds == 1,
              "diagnostic alternate option forms and supported time boundaries");
        for (const auto& arguments : std::vector<std::vector<std::string>>{
                 {"--smoke-dir"}, {"--smoke-dir="}, {"--smoke-dir", "relative/path"},
                 {"--smoke-seconds"}, {"--smoke-seconds="}, {"--smoke-seconds=nan"},
                 {"--smoke-seconds=inf"}, {"--smoke-seconds=0"}, {"--smoke-seconds=601"},
                 {"--smoke-seconds=25junk"}, {"--smoke-capture-at=1,"},
                 {"--smoke-capture-at=nan"}, {"--smoke-keys=3"}, {"--smoke-keys=3:"}}) {
            rejected = false;
            try { parse(arguments); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "invalid diagnostic arguments are rejected during parsing");
        }
        nocturne::StartupOptions smoke_base;
        smoke_base.data_dir = data;
        smoke_base.user_dir = temporary / "rejected smoke profile";
        const auto reject_smoke = [&](const nocturne::StartupOptions& invalid) {
            const auto cwd = fs::current_path();
            bool failed = false;
            try { nocturne::prepare_runtime(invalid); }
            catch (const std::exception&) { failed = true; }
            check(failed && !fs::exists(invalid.user_dir) && fs::current_path() == cwd,
                  "invalid diagnostics fail before profile creation or cwd changes");
        };
        for (const auto& output : {captures / "missing", data / "GAME.POD",
                                   fs::path("relative"), temporary / "asset link"}) {
            auto invalid = smoke_base;
            invalid.smoke_dir = output;
            reject_smoke(invalid);
        }
        for (const double seconds : {0.0, 601.0, std::numeric_limits<double>::infinity()}) {
            auto invalid = smoke_base;
            invalid.smoke_seconds = seconds;
            reject_smoke(invalid);
        }
        {
            auto invalid = smoke_base;
            invalid.smoke_capture_at = "-1";
            reject_smoke(invalid);
        }
        setenv("NOCTURNE_SMOKE_DIR", "/inherited/diagnostics", 1);
        setenv("NOCTURNE_SMOKE_SECONDS", "99", 1);
        setenv("NOCTURNE_SMOKE_CAPTURE_AT", "7", 1);
        setenv("NOCTURNE_SMOKE_KEYS", "8:Return", 1);
        smoke_base.user_dir = user;
        check(nocturne::prepare_runtime(smoke_base) &&
              std::string(std::getenv("NOCTURNE_SMOKE_DIR")) == "/inherited/diagnostics" &&
              std::string(std::getenv("NOCTURNE_SMOKE_SECONDS")) == "99" &&
              std::string(std::getenv("NOCTURNE_SMOKE_CAPTURE_AT")) == "7" &&
              std::string(std::getenv("NOCTURNE_SMOKE_KEYS")) == "8:Return",
              "no diagnostic flags preserves the existing environment workflow");
        diagnostic.data_dir = data;
        diagnostic.user_dir = temporary / "queried smoke profile";
        diagnostic.print_paths = true;
        const auto query_cwd = fs::current_path();
        check(!nocturne::prepare_runtime(diagnostic) && !fs::exists(diagnostic.user_dir) &&
              fs::current_path() == query_cwd &&
              std::string(std::getenv("NOCTURNE_SMOKE_DIR")) == "/inherited/diagnostics" &&
              std::string(std::getenv("NOCTURNE_SMOKE_SECONDS")) == "99" &&
              std::string(std::getenv("NOCTURNE_SMOKE_CAPTURE_AT")) == "7" &&
              std::string(std::getenv("NOCTURNE_SMOKE_KEYS")) == "8:Return",
              "informational diagnostic paths do not mutate files, cwd or environment");
        diagnostic.print_paths = false;
        check(nocturne::prepare_runtime(diagnostic) &&
              std::string(std::getenv("NOCTURNE_SMOKE_DIR")) == captures.string() &&
              std::stod(std::getenv("NOCTURNE_SMOKE_SECONDS")) == 25.5 &&
              std::string(std::getenv("NOCTURNE_SMOKE_CAPTURE_AT")) == "1,5,10" &&
              std::string(std::getenv("NOCTURNE_SMOKE_KEYS")) == "3:Escape" &&
              nocturne::game_command_line() == "nocturne -window",
              "successful startup activates explicit diagnostics outside recovered options");
        smoke_base.smoke_seconds = 2;
        smoke_base.smoke_keys = "";
        check(nocturne::prepare_runtime(smoke_base) &&
              std::string(std::getenv("NOCTURNE_SMOKE_DIR")) == captures.string() &&
              std::stod(std::getenv("NOCTURNE_SMOKE_SECONDS")) == 2 &&
              std::string(std::getenv("NOCTURNE_SMOKE_CAPTURE_AT")) == "1,5,10" &&
              std::string(std::getenv("NOCTURNE_SMOKE_KEYS")).empty(),
              "partial diagnostic flags change only explicit fields and allow clearing a schedule");
        fs::current_path(original_cwd);
        fs::remove_all(temporary);
        std::cout << "PS4 host runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        fs::current_path(original_cwd);
        if (!temporary.empty()) fs::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#if defined(NOCTURNE_PS4)
// Firmware adapter doubles let prepare_runtime exercise actual host files.
// The component walker is shared with production; filesystem_tests separately
// covers the native adapter's virtual cwd and no-follow inspection operations.
int nocturne_ps4_resolve_directory(const char* path, char* output, std::size_t capacity,
                                  bool allow_missing_tail) {
    const auto cwd = fs::current_path().string();
    struct stat status{};
    const auto mode = allow_missing_tail ? nocturne::ps4::ResolveMode::PlannedDirectory :
                                          nocturne::ps4::ResolveMode::Existing;
    if (nocturne::ps4::resolve_path(path, cwd.c_str(), output, capacity, lstat, mode, &status) != 0)
        return -1;
    if (!allow_missing_tail && !S_ISDIR(status.st_mode)) { errno = ENOTDIR; return -1; }
    return 0;
}
int nocturne_ps4_chdir(const char* path) {
    ++chdir_calls;
    if (denied_chdir_error) { errno = denied_chdir_error; return -1; }
    return ::chdir(path);
}
#endif
