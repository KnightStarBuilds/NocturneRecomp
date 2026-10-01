// =============================================================================
// GL API — dynamically loaded OpenGL entry points
// =============================================================================
//
// See gl_api.h for why these are loaded rather than linked.

#include "shim_config.h"

#if NOCTURNE_GL_PRESENT

#include "gl/gl_api.h"
#include "core/debug_log.h"

#include <SDL.h>

struct NocturneGLApi gl;

namespace {

bool g_loaded = false;
bool g_failed = false;

// Resolve one entry point, recording the first failure so the caller can report
// a specific missing symbol rather than a blanket "GL unavailable".
template <typename Fn>
void load_one(Fn *slot, const char *name, const char **first_missing) {
    *slot = reinterpret_cast<Fn>(SDL_GL_GetProcAddress(name));
    if (*slot == nullptr && *first_missing == nullptr) {
        *first_missing = name;
    }
}

}  // namespace

#define NOCTURNE_GL_LOAD(field) load_one(&gl.field, "gl" #field, &missing)

extern "C" int nocturne_gl_load_api(void) {
    if (g_loaded) return 1;
    if (g_failed) return 0;

    const char *missing = nullptr;

    NOCTURNE_GL_LOAD(GetString);
    NOCTURNE_GL_LOAD(GetError);
    NOCTURNE_GL_LOAD(GetIntegerv);

    NOCTURNE_GL_LOAD(Viewport);
    NOCTURNE_GL_LOAD(Scissor);
    NOCTURNE_GL_LOAD(ClearColor);
    NOCTURNE_GL_LOAD(ClearDepth);
    NOCTURNE_GL_LOAD(Clear);
    NOCTURNE_GL_LOAD(PixelStorei);
    NOCTURNE_GL_LOAD(ReadBuffer);
    NOCTURNE_GL_LOAD(ReadPixels);
    NOCTURNE_GL_LOAD(GetTexImage);

    NOCTURNE_GL_LOAD(Enable);
    NOCTURNE_GL_LOAD(Disable);
    NOCTURNE_GL_LOAD(BlendFunc);
    NOCTURNE_GL_LOAD(DepthFunc);
    NOCTURNE_GL_LOAD(DepthMask);
    NOCTURNE_GL_LOAD(DepthRange);
    NOCTURNE_GL_LOAD(CullFace);
    NOCTURNE_GL_LOAD(FrontFace);
    NOCTURNE_GL_LOAD(PolygonMode);
    NOCTURNE_GL_LOAD(PolygonOffset);

    NOCTURNE_GL_LOAD(GenFramebuffers);
    NOCTURNE_GL_LOAD(DeleteFramebuffers);
    NOCTURNE_GL_LOAD(BindFramebuffer);
    NOCTURNE_GL_LOAD(FramebufferTexture2D);
    NOCTURNE_GL_LOAD(CheckFramebufferStatus);
    NOCTURNE_GL_LOAD(GenRenderbuffers);
    NOCTURNE_GL_LOAD(DeleteRenderbuffers);
    NOCTURNE_GL_LOAD(BindRenderbuffer);
    NOCTURNE_GL_LOAD(RenderbufferStorage);
    NOCTURNE_GL_LOAD(FramebufferRenderbuffer);
    NOCTURNE_GL_LOAD(BlitFramebuffer);

    NOCTURNE_GL_LOAD(GenTextures);
    NOCTURNE_GL_LOAD(DeleteTextures);
    NOCTURNE_GL_LOAD(BindTexture);
    NOCTURNE_GL_LOAD(TexParameteri);
    NOCTURNE_GL_LOAD(TexImage2D);
    NOCTURNE_GL_LOAD(TexSubImage2D);

    NOCTURNE_GL_LOAD(DrawElements);

    // Core-profile drawing needs every shader, buffer and VAO entry point.
    // Resolve the complete contract before any renderer path can dereference it.
    NOCTURNE_GL_LOAD(CreateShader);
    NOCTURNE_GL_LOAD(ShaderSource);
    NOCTURNE_GL_LOAD(CompileShader);
    NOCTURNE_GL_LOAD(GetShaderiv);
    NOCTURNE_GL_LOAD(GetShaderInfoLog);
    NOCTURNE_GL_LOAD(DeleteShader);
    NOCTURNE_GL_LOAD(CreateProgram);
    NOCTURNE_GL_LOAD(AttachShader);
    NOCTURNE_GL_LOAD(LinkProgram);
    NOCTURNE_GL_LOAD(GetProgramiv);
    NOCTURNE_GL_LOAD(GetProgramInfoLog);
    NOCTURNE_GL_LOAD(DeleteProgram);
    NOCTURNE_GL_LOAD(UseProgram);
    NOCTURNE_GL_LOAD(GetUniformLocation);
    NOCTURNE_GL_LOAD(Uniform1i);
    NOCTURNE_GL_LOAD(Uniform1f);
    NOCTURNE_GL_LOAD(Uniform2f);
    NOCTURNE_GL_LOAD(Uniform3f);
    NOCTURNE_GL_LOAD(Uniform4f);
    NOCTURNE_GL_LOAD(ActiveTexture);

    NOCTURNE_GL_LOAD(GenBuffers);
    NOCTURNE_GL_LOAD(DeleteBuffers);
    NOCTURNE_GL_LOAD(BindBuffer);
    NOCTURNE_GL_LOAD(BufferData);
    NOCTURNE_GL_LOAD(BufferSubData);
    NOCTURNE_GL_LOAD(GetAttribLocation);
    NOCTURNE_GL_LOAD(BindAttribLocation);
    NOCTURNE_GL_LOAD(EnableVertexAttribArray);
    NOCTURNE_GL_LOAD(DisableVertexAttribArray);
    NOCTURNE_GL_LOAD(VertexAttribPointer);
    NOCTURNE_GL_LOAD(UniformMatrix4fv);
    NOCTURNE_GL_LOAD(DrawArrays);
    NOCTURNE_GL_LOAD(GenVertexArrays);
    NOCTURNE_GL_LOAD(DeleteVertexArrays);
    NOCTURNE_GL_LOAD(BindVertexArray);
    NOCTURNE_GL_LOAD(GenerateMipmap);

    if (missing != nullptr) {
        DLOG("render","gl_api: failed to resolve %s — GL path unavailable", missing);
        g_failed = true;
        return 0;
    }

    g_loaded = true;
    return 1;
}

#undef NOCTURNE_GL_LOAD

#else  // !NOCTURNE_GL_PRESENT

#include "gl/gl_api.h"
struct NocturneGLApi gl;
extern "C" int nocturne_gl_load_api(void) { return 0; }

#endif  // NOCTURNE_GL_PRESENT
