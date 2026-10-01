#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace nocturne {
struct StartupOptions {
    std::filesystem::path data_dir;
    std::filesystem::path user_dir;
    std::vector<std::string> game_args;
    bool help = false;
    bool print_paths = false;
};
StartupOptions parse_options(int argc, char** argv);
std::filesystem::path executable_path();
// Returns false for informational options; throws with a readable startup error.
bool prepare_runtime(const StartupOptions& options);
const std::string& game_command_line();
}
