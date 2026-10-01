// Exercise the actual presenter and trigl shaders on the native GL driver.
#include "gl/gl_api.h"
#include "gl/gl_present.h"
#include "renderer/trigl_gl.h"
#include <SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

SDL_Window *g_sdlWindow = nullptr;

namespace {
void require(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "%s (SDL: %s)\n", message, SDL_GetError());
        std::exit(1);
    }
}

void pixel(int x, int y, int red, int green, int blue, const char *message) {
    std::array<unsigned char, 4> actual{};
    gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    if (std::abs(int(actual[0]) - red) > 2 ||
        std::abs(int(actual[1]) - green) > 2 ||
        std::abs(int(actual[2]) - blue) > 2) {
        std::fprintf(stderr, "%s: got %u,%u,%u; expected %d,%d,%d\n", message,
                     actual[0], actual[1], actual[2], red, green, blue);
        std::exit(1);
    }
}

void quad(float z, unsigned color, const NocturneTriglPipelineState &state) {
    NocturneTriglScreenVertex vertices[] = {
        {0, 0, z, 1, color, 0xff000000, 0, 0},
        {32, 0, z, 1, color, 0xff000000, 1, 0},
        {32, 32, z, 1, color, 0xff000000, 1, 1},
        {0, 32, z, 1, color, 0xff000000, 0, 1},
    };
    unsigned short indices[] = {0, 1, 2, 0, 2, 3};
    NocturneTriglBatch batch{vertices, indices, 4, 6, 4, 6};
    nocturne_trigl_gl_apply_state(&state);
    nocturne_trigl_gl_draw_batch(&batch);
}
}

int main() {
    SDL_SetMainReady();
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video initialization failed");
    nocturne_gl_prepare_window();
    g_sdlWindow = SDL_CreateWindow("Nocturne GL validation", 0, 0, 640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
    require(g_sdlWindow != nullptr, "Native window creation failed");
    require(nocturne_gl_init(g_sdlWindow), "Production GL context/API failed");
    std::printf("GL %s; renderer %s\n", gl.GetString(GL_VERSION), gl.GetString(GL_RENDERER));

    GLint major = 0, minor = 0, profile = 0;
    gl.GetIntegerv(GL_MAJOR_VERSION, &major);
    gl.GetIntegerv(GL_MINOR_VERSION, &minor);
    gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    require((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0, "Expected core GL context");
#ifdef __APPLE__
    require(major > 4 || (major == 4 && minor >= 1), "Expected macOS GL 4.1");
#endif
    require(nocturne_gl_scene_target_bind(32, 32), "Scene framebuffer incomplete");
    nocturne_gl_set_logical_size(640, 480);
    gl.ClearDepth(0.25);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    // Memory is top-down ARGB; readback is bottom-up RGBA.
    const unsigned image[] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    nocturne_gl_scene_upload(image, 2, 2, 2 * sizeof(unsigned), 32);
    pixel(8, 24, 255, 0, 0, "Presenter top-left orientation");
    pixel(24, 24, 0, 255, 0, "Presenter top-right orientation");
    pixel(8, 8, 0, 0, 255, "Presenter bottom-left orientation");
    pixel(24, 8, 255, 255, 255, "Presenter bottom-right orientation");
    float depth = 0;
    gl.ReadPixels(16, 16, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    require(std::abs(depth - 0.25f) < 0.00001f, "CPU upload changed scene depth");

    require(nocturne_trigl_gl_init(), "Production trigl shader compilation failed");
    nocturne_trigl_gl_set_target_size(32, 32);
    nocturne_trigl_debug = 0;
    nocturne_trigl_vertex_fog = 0;
    nocturne_trigl_overlay_bias = 0;
    gl.ClearDepth(1);
    gl.DepthMask(GL_TRUE);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    NocturneTriglPipelineState opaque{};
    opaque.depth_test_enabled = opaque.depth_write_enabled = 1;
    opaque.depth_func = NOCTURNE_TRIGL_DEPTH_LEQUAL;
    quad(0.25f, 0xffff0000, opaque);
    quad(0.75f, 0xff0000ff, opaque);
    pixel(16, 16, 255, 0, 0, "trigl depth occlusion");
    require(nocturne_trigl_gl_save_depth(0, 32, 32), "Master depth save failed");
    gl.ClearDepth(1);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    require(nocturne_trigl_gl_restore_depth(0, 0, 0, 32, 32, 32, 32),
            "Master depth restore failed");
    quad(0.75f, 0xff00ff00, opaque);
    pixel(16, 16, 255, 0, 0, "Restored master depth occlusion");

    auto blend = opaque;
    blend.blend_enabled = 1;
    blend.depth_write_enabled = 0;
    blend.src_blend = NOCTURNE_TRIGL_BLEND_SRC_ALPHA;
    blend.dst_blend = NOCTURNE_TRIGL_BLEND_INV_SRC_ALPHA;
    quad(0.1f, 0x800000ff, blend);
    pixel(16, 16, 127, 0, 128, "trigl transparency");
    nocturne_gl_present_scene();
    require(gl.GetError() == GL_NO_ERROR, "OpenGL error during pixel validation");

    nocturne_trigl_gl_shutdown();
    nocturne_gl_shutdown();
    SDL_DestroyWindow(g_sdlWindow);
    SDL_Quit();
    std::puts("Native FBO, presenter orientation, depth preservation, trigl shaders, "
              "depth occlusion, master depth restore and transparency passed");
}
