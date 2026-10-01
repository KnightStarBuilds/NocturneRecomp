#include "runtime_paths.h"
#include "text_stream.h"
#include "watcom/path.h"
#include "core/file_search.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif
namespace fs = std::filesystem;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
int main() {
    const auto old = fs::current_path();
    const auto root = fs::temp_directory_path() / ("Nocturne Recomp (KnightStar) paths " + std::to_string(
#if defined(_WIN32)
    std::rand()
#else
    getpid()
#endif
    ));
    try {
        fs::create_directories(root / "Game Data/SYSTEM");
        fs::create_directories(root / "Game Data/Video");
        fs::create_directories(root / "Game Data/SAVE");
        std::ofstream(root / "Game Data/ACT1.POD") << "dummy archive";
        std::ofstream(root / "Game Data/Z.POD") << "archive sorted before lowercase";
        std::ofstream(root / "Game Data/a.POD") << "archive sorted after uppercase";
        std::ofstream(root / "Game Data/SAVE/original.sav") << "immutable original save";
        std::ofstream(root / "Game Data/SYSTEM/RENDER.INI") << "immutable renderer config";
        std::ofstream(root / "Game Data/Video/Opening.AVI") << "video";
        std::ofstream(root / "Game Data/SYSTEM/nocturne.ini") << "[Graphics]\r\ngamePIXX=1024\r\ngamePIXY=768\r\nuseDirect3D=0\r\nfirstTimeFlag=1\r\nrendererDLLPath=tridx7.dll\r\n[Sound]\r\nMute=0\r\n";
        std::string data = (root / "Game Data").string(), user = (root / "User Data").string();
        char name[] = "Nocturne Recomp (KnightStar)", da[] = "--data-dir", ua[] = "--user-dir";
        char* args[] = {name, da, data.data(), ua, user.data()};
        auto options = nocturne::parse_options(5, args);
        check(nocturne::prepare_runtime(options), "runtime setup");
        check(fs::equivalent(fs::current_path(), user), "working directory must be user dir");
        check(fs::equivalent(watcom_resolve_fs_path("vIdEo\\oPeNiNg.AvI"), root / "Game Data/Video/Opening.AVI"), "case folded data lookup");
        const auto expanded_asset = (root / "User Data/Video/Opening.AVI").string();
        check(fs::equivalent(watcom_resolve_fs_path(expanded_asset.c_str()), root / "Game Data/Video/Opening.AVI"), "asset lookup survives original _fullpath expansion under user directory");
        check(watcom_resolve_fs_path(expanded_asset.c_str(), true) == expanded_asset, "expanded write path remains inside profile");
        const auto external_asset = (root / "Unrelated/Video/Opening.AVI").string();
        check(watcom_resolve_fs_path(external_asset.c_str()) == external_asset, "unrelated absolute paths do not use asset overlay");
        check(watcom_resolve_fs_path("ACT1.POD", true) == "ACT1.POD", "writes must not select assets");
        const auto files = nocturne_find_files("*.pod");
        check(files == std::vector<std::string>({"ACT1.POD", "Z.POD", "a.POD"}), "POD discovery preserves upstream bytewise priority");
        std::ofstream("act1.pod") << "override";
        check(nocturne_find_files("*.POD").size() == 3, "overlay deduplicates case");
        check(fs::equivalent(watcom_resolve_fs_path("ACT1.POD"), root / "User Data/act1.pod"), "user override wins reads");
        check(nocturne_find_files("SAVE\\*.sav").empty(), "save discovery never falls back to original data");
        check(!fs::exists(watcom_resolve_fs_path("SAVE\\original.sav")), "save read never falls back to original data");
        check(!fs::exists(watcom_resolve_fs_path("SYSTEM\\render.ini")), "config read never falls back to original data");
        check(nocturne_find_files("vIdEo\\*.avi") == std::vector<std::string>({"vIdEo/Opening.AVI"}), "asset subdirectory search folds case");
        check(!fs::exists(watcom_resolve_fs_path("Video\\Opening.AVI", true)), "new media writes never choose data overlay");
        std::ifstream ini("system/nocturne.ini");
        const std::string ini_text((std::istreambuf_iterator<char>(ini)), {});
        check(ini_text.find("gamePIXX=640") != std::string::npos && ini_text.find("gamePIXY=480") != std::string::npos, "initial logical framebuffer");
        check(ini_text.find("useDirect3D=1") != std::string::npos && ini_text.find("rendererDLLPath=trigl.dll") != std::string::npos && ini_text.find("firstTimeFlag=0") != std::string::npos, "new profile enables native renderer without first-run override");
        check(ini_text.find("[Sound]\nMute=0") != std::string::npos, "initial config retains unrelated settings");
        std::ofstream("system/nocturne.ini", std::ios::app) << "sentinel=42\n";
        nocturne::prepare_runtime(options);
        std::ifstream persisted("system/nocturne.ini");
        const std::string saved((std::istreambuf_iterator<char>(persisted)), {});
        check(saved.find("sentinel=42") != std::string::npos, "relaunch preserves user config");
        // Without an explicit data path, a profile remembers its own selection
        // even when this executable lives next to a different project's data.
        env("NOCTURNE_DATA_DIR", "");
        auto remembered_options = options;
        remembered_options.data_dir.clear();
        nocturne::prepare_runtime(remembered_options);
        check(fs::equivalent(std::getenv("NOCTURNE_DATA_DIR"), options.data_dir), "custom profile remembers selected assets");
        auto boundary_options = options;
        boundary_options.game_args.assign(123, "x");
        nocturne::prepare_runtime(boundary_options);
        check(nocturne::game_command_line().size() == 254, "recovered command line accepts safe byte boundary");
        const auto reject_before_write = [&](nocturne::StartupOptions invalid_options) {
            invalid_options.user_dir = root / "Rejected Profile";
            bool rejected = false;
            try { nocturne::prepare_runtime(invalid_options); }
            catch (const std::exception&) { rejected = true; }
            check(rejected && !fs::exists(invalid_options.user_dir), "invalid game arguments rejected before profile writes");
        };
        boundary_options.game_args.push_back("x");
        reject_before_write(boundary_options);
        for (unsigned char control = 0; control <= 0x20; ++control) {
            auto invalid_options = options;
            invalid_options.game_args = {std::string("prefix") + static_cast<char>(control) + "suffix"};
            reject_before_write(invalid_options);
        }
        boundary_options.game_args = {""};
        reject_before_write(boundary_options);
        auto nested_options = options;
        nested_options.user_dir = options.data_dir / "Disallowed Profile";
        bool nested_rejected = false;
        try { nocturne::prepare_runtime(nested_options); }
        catch (const std::exception&) { nested_rejected = true; }
        check(nested_rejected && !fs::exists(nested_options.user_dir), "profile cannot mutate original data tree");
        // Missing/partial original configuration still gets all native settings.
        fs::create_directories(root / "Bare Assets");
        std::ofstream(root / "Bare Assets/BASE.POD") << "dummy";
        auto bare_options = options;
        bare_options.data_dir = root / "Bare Assets";
        bare_options.user_dir = root / "Bare Profile";
        nocturne::prepare_runtime(bare_options);
        std::ifstream bare_ini("system/nocturne.ini");
        const std::string defaults((std::istreambuf_iterator<char>(bare_ini)), {});
        check(defaults.find("firstTimeFlag=0") != std::string::npos && defaults.find("useDirect3D=1") != std::string::npos, "missing original config receives native renderer defaults");
        fs::current_path(root);
        // Mimics a POD member at a physical offset, with CRLF and libc-sized
        // boundaries. ftell positions must be seekable after translated reads.
        const std::string prefix(8191, 'x');
        std::ofstream raw("stream.txt", std::ios::binary);
        raw << prefix << "\r\nfirst\r\nsecond\r\n"; raw.close();
        FILE* f = nocturne_fopen_text("stream.txt", "rt");
        check(f != nullptr, "open text stream");
        check(fseek(f, 8191, SEEK_SET) == 0 && fgetc(f) == '\n', "physical POD offset seek");
        check(ftell(f) == 8193, "physical tell after CRLF");
        char buffer[32]{};
        check(fgets(buffer, sizeof(buffer), f) && std::string(buffer) == "first\n", "CRLF decoding");
        const long position = ftell(f);
        check(position == 8200, "position after translated line");
        check(fgets(buffer, sizeof(buffer), f) && std::string(buffer) == "second\n", "second line");
        check(fseek(f, position, SEEK_SET) == 0 && fgets(buffer, sizeof(buffer), f) && std::string(buffer) == "second\n", "seek/tell round trip");
        rewind(f); check(fgetc(f) == 'x', "rewind"); fclose(f);
        f = nocturne_fopen_text("stream.txt", "rb");
        fseek(f, 8191, SEEK_SET); check(fgetc(f) == '\r', "binary mode remains raw"); fclose(f);
        f = nocturne_fopen_text("written.txt", "wt"); check(f != nullptr, "text write open");
        check(fputs("one\ntwo\n", f) >= 0 && fclose(f) == 0, "text write close");
        char empty[] = "--data-dir="; char* invalid[] = {name, empty};
        bool rejected = false;
        try { nocturne::parse_options(2, invalid); } catch (const std::exception&) { rejected = true; }
        check(rejected, "empty path rejected");
        fs::current_path(old); fs::remove_all(root);
        std::cout << "Native paths, persistence and text seek/tell passed\n";
        return 0;
    } catch (const std::exception& error) {
        fs::current_path(old);
        std::cerr << error.what() << " (fixture " << root << ")\n";
        return 1;
    }
}
