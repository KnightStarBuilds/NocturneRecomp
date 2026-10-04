#include "smoke_probe.h"
#include "gl/gl_api.h"
#include "gl/gl_present.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
struct Key { Uint32 at; SDL_Keycode code; };
struct Mouse { Uint32 at; int x; int y; };
struct Probe {
    std::filesystem::path directory;
    std::vector<Uint32> captures;
    std::vector<Key> keys;
    std::vector<Key> releases;
    std::vector<Mouse> mouse;
    FILE* events = nullptr;
    FILE* audio = nullptr;
    Uint32 start = 0;
    Uint32 duration = 30000;
    unsigned frame = 0;
    bool quit_sent = false;

    Probe() {
        const char* dir = std::getenv("NOCTURNE_SMOKE_DIR");
        if (!dir || !*dir) return;
        directory = dir;
        if (!directory.is_absolute() || !std::filesystem::is_directory(directory)) return;
        events = std::fopen((directory / "events.csv").string().c_str(), "w");
        if (!events) return;
        std::setvbuf(events, nullptr, _IOLBF, 0);
        std::fputs("elapsed_ms,event,detail\n", events);
        audio = std::fopen((directory / "movie-audio.csv").string().c_str(), "w");
        if (audio) {
            std::setvbuf(audio, nullptr, _IOLBF, 0);
            std::fputs("elapsed_ms,rate,channels,samples,nonzero,peak,rms\n", audio);
        }
        start = SDL_GetTicks();
        const char* seconds = std::getenv("NOCTURNE_SMOKE_SECONDS");
        if (seconds) {
            const double value = std::atof(seconds);
            if (std::isfinite(value)) duration = Uint32(std::clamp(value, 1.0, 600.0) * 1000);
        }
        parse(std::getenv("NOCTURNE_SMOKE_CAPTURE_AT"), false);
        parse(std::getenv("NOCTURNE_SMOKE_KEYS"), true);
        parse_mouse(std::getenv("NOCTURNE_SMOKE_MOUSE_AT"));
    }

    void parse(const char* value, bool key_list) {
        if (!value) return;
        std::string input(value);
        std::size_t at = 0;
        while (at < input.size()) {
            const auto end = input.find(',', at);
            const auto item = input.substr(at, end - at);
            char* tail = nullptr;
            const double time = std::strtod(item.c_str(), &tail);
            if (std::isfinite(time) && time >= 0 && time <= 600 && tail != item.c_str()) {
                if (key_list && *tail == ':') {
                    const auto code = SDL_GetKeyFromName(tail + 1);
                    if (code != SDLK_UNKNOWN && keys.size() < 128) keys.push_back({Uint32(time * 1000), code});
                } else if (!key_list && *tail == '\0' && captures.size() < 64) {
                    captures.push_back(Uint32(time * 1000));
                }
            }
            if (end == std::string::npos) break;
            at = end + 1;
        }
        std::sort(captures.begin(), captures.end());
        std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.at < b.at; });
    }

    void parse_mouse(const char* value) {
        if (!value) return;
        std::string input(value);
        std::size_t at = 0;
        while (at < input.size()) {
            const auto end = input.find(',', at);
            const auto item = input.substr(at, end - at);
            double time = 0, x = 0, y = 0;
            char extra = 0;
            if (std::sscanf(item.c_str(), "%lf:%lf:%lf%c", &time, &x, &y, &extra) == 3 &&
                std::isfinite(time) && time >= 0 && time <= 600 &&
                std::isfinite(x) && x >= 0 && x < 16384 && x == std::floor(x) &&
                std::isfinite(y) && y >= 0 && y < 16384 && y == std::floor(y) && mouse.size() < 128) {
                mouse.push_back({Uint32(time * 1000), int(x), int(y)});
            }
            if (end == std::string::npos) break;
            at = end + 1;
        }
        std::stable_sort(mouse.begin(), mouse.end(), [](const Mouse& a, const Mouse& b) { return a.at < b.at; });
    }
};

Probe& probe() { static Probe result; return result; }

void key_event(SDL_Keycode code, Uint32 type) {
    SDL_Event event{};
    event.type = type;
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = code;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
    SDL_PushEvent(&event);
}

void capture(Probe& p, SDL_Window* window, SDL_Renderer* renderer, Uint32 elapsed) {
    int width = 0, height = 0;
    if (renderer) SDL_GetRendererOutputSize(renderer, &width, &height);
    else SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return;
    std::vector<unsigned char> pixels(std::size_t(width) * height * 3);
    if (renderer) {
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24,
                                 pixels.data(), width * 3) != 0) return;
    } else {
    GLint framebuffer = 0, buffer = 0, alignment = 0, row_length = 0;
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    gl.GetIntegerv(GL_READ_BUFFER, &buffer);
    gl.GetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    gl.GetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.ReadBuffer(GL_BACK);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.ReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    gl.PixelStorei(GL_PACK_ALIGNMENT, alignment);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, row_length);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(framebuffer));
    gl.ReadBuffer(GLenum(buffer));
    }
    char name[80];
    std::snprintf(name, sizeof(name), "frame-%06u-ms-%04u.ppm", elapsed, p.frame++);
    FILE* image = std::fopen((p.directory / name).string().c_str(), "wb");
    if (!image) return;
    std::fprintf(image, "P6\n%d %d\n255\n", width, height);
    // Capture the completed backbuffer immediately before swap; convert GL's
    // bottom-left origin to the top-down file convention without rendering again.
    for (int row = 0; row < height; ++row) {
        const int source_row = renderer ? row : height - row - 1;
        std::fwrite(pixels.data() + std::size_t(source_row) * width * 3, 3, width, image);
    }
    std::fclose(image);
    std::fprintf(p.events, "%u,capture,%s\n", elapsed, name);
    // Native cursors are composed by the window system, outside this GL image.
    // Query visibility without changing it so each capture has both signals.
    std::fprintf(p.events, "%u,native_cursor,%d\n", elapsed, SDL_ShowCursor(SDL_QUERY));
}
}

static void before_present(SDL_Window* window, SDL_Renderer* renderer) {
    auto& p = probe();
    if (!p.events) return;
    const Uint32 elapsed = SDL_GetTicks() - p.start;
    while (!p.captures.empty() && elapsed >= p.captures.front()) {
        capture(p, window, renderer, elapsed);
        p.captures.erase(p.captures.begin());
    }
    while (!p.releases.empty() && elapsed >= p.releases.front().at) {
        key_event(p.releases.front().code, SDL_KEYUP);
        p.releases.erase(p.releases.begin());
    }
    while (!p.keys.empty() && elapsed >= p.keys.front().at) {
        const auto key = p.keys.front();
        key_event(key.code, SDL_KEYDOWN);
        p.releases.push_back({elapsed + 150, key.code});
        std::fprintf(p.events, "%u,key,%s\n", elapsed, SDL_GetKeyName(key.code));
        p.keys.erase(p.keys.begin());
    }
    while (!p.mouse.empty() && elapsed >= p.mouse.front().at) {
        const auto motion = p.mouse.front();
        SDL_Event event{};
        event.type = SDL_MOUSEMOTION;
        event.motion.windowID = SDL_GetWindowID(window);
        // Schedule engine framebuffer coordinates. The bridge maps window
        // coordinates back through the same Retina/aspect transform on input.
        nocturne_gl_logical_to_window(motion.x, motion.y, &event.motion.x, &event.motion.y);
        const int queued = SDL_PushEvent(&event);
        std::fprintf(p.events, "%u,mouse,logical=%d:%d window=%d:%d queued=%d\n", elapsed,
                     motion.x, motion.y, event.motion.x, event.motion.y, queued);
        p.mouse.erase(p.mouse.begin());
    }
    if (elapsed >= p.duration && !p.quit_sent) {
        SDL_Event event{};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        p.quit_sent = true;
        std::fprintf(p.events, "%u,quit,requested\n", elapsed);
    }
}

void nocturne_smoke_before_swap(SDL_Window* window) { before_present(window, nullptr); }

void nocturne_smoke_before_sdl_present(SDL_Window* window, SDL_Renderer* renderer) {
    if (renderer) before_present(window, renderer);
}

void nocturne_smoke_audio_queued(const void* pcm, std::size_t bytes, int rate, int channels) {
    auto& p = probe();
    if (!p.audio || !pcm) return;
    const auto* data = static_cast<const unsigned char*>(pcm);
    std::size_t nonzero = 0;
    int peak = 0;
    double squares = 0;
    const std::size_t count = bytes / sizeof(std::int16_t);
    for (std::size_t i = 0; i < count; ++i) {
        std::int16_t value;
        std::memcpy(&value, data + i * sizeof(value), sizeof(value));
        if (value) ++nonzero;
        peak = std::max(peak, std::abs(int(value)));
        squares += double(value) * value;
    }
    std::fprintf(p.audio, "%u,%d,%d,%zu,%zu,%d,%.3f\n", SDL_GetTicks() - p.start,
                 rate, channels, count, nonzero, peak, count ? std::sqrt(squares / count) : 0);
}
