/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* PS4 software VideoOut backend for SDL 2.30.0.
 * Driver scaffolding adapted from SDL's dummy video/framebuffer drivers.
 * Altered for Nocturne; see LICENSE.txt for the original SDL zlib notice.
 */
#include "SDL_internal.h"
#include "video/SDL_sysvideo.h"
#include "events/SDL_keyboard_c.h"
#include "events/SDL_mouse_c.h"
#include "SDL_hints.h"
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/SystemService.h>
#include "presentation.h"
#include "modules.h"

typedef struct {
    int handle;
    int registration;
    OrbisKernelEqueue queue;
    off_t physical;
    void *memory;
    size_t size;
    size_t stride;
    void *buffers[2];
    unsigned current;
    SDL_bool in_flight;
    int width, height;
    SDL_Window *window;
} PS4_Video;

static int ps4_error(const char *operation, int result)
{
    return SDL_SetError("%s failed: 0x%08x", operation, (unsigned)result);
}

static void PS4_VideoQuit(_THIS)
{
    PS4_Video *v = _this->driverdata;
    if (v->handle >= 0) {
        if (v->registration >= 0) sceVideoOutUnregisterBuffers(v->handle, v->registration);
        sceVideoOutClose(v->handle);
        v->handle = -1;
    }
    if (v->queue) { sceKernelDeleteEqueue(v->queue); v->queue = 0; }
    if (v->memory) { sceKernelMunmap(v->memory, v->size); v->memory = NULL; }
    if (v->physical >= 0) { sceKernelReleaseDirectMemory(v->physical, v->size); v->physical = -1; }
}

static int PS4_VideoInit(_THIS)
{
    PS4_Video *v = _this->driverdata;
    OrbisVideoOutBufferAttribute attr;
    SDL_DisplayMode mode;
    int ret;
    if (ps4_load_module(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT, "VideoOut") < 0 ||
        ps4_load_module(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE, "SystemService") < 0)
        return -1;
    /* VideoOut accepts a 1080p surface on both base and Pro consoles. SDL's
     * virtual desktop stays at game resolution; scaling happens once below. */
    v->width = 1920;
    v->height = 1080;
    v->handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (v->handle < 0) return ps4_error("sceVideoOutOpen", v->handle);
    v->stride = ((size_t)v->width * v->height * 4 + 0x1fffff) & ~(size_t)0x1fffff;
    v->size = v->stride * 2;
    ret = sceKernelAllocateDirectMemory(0, ORBIS_KERNEL_MAIN_DMEM_SIZE, v->size,
                                      0x200000, ORBIS_KERNEL_WC_GARLIC, &v->physical);
    if (ret < 0) goto fail;
    ret = sceKernelMapDirectMemory(&v->memory, v->size,
                                  ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW,
                                  0, v->physical, 0x200000);
    if (ret < 0) goto fail;
    SDL_memset(v->memory, 0, v->size);
    v->buffers[0] = v->memory;
    v->buffers[1] = (Uint8 *)v->memory + v->stride;
    SDL_zero(attr);
    attr.format = ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB;
    attr.tmode = ORBIS_VIDEO_OUT_TILING_MODE_LINEAR;
    attr.aspect = ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9;
    attr.width = v->width;
    attr.height = v->height;
    attr.pixelPitch = v->width;
    ret = sceVideoOutRegisterBuffers(v->handle, 0, v->buffers, 2, &attr);
    if (ret < 0) goto fail;
    v->registration = ret;
    ret = sceKernelCreateEqueue(&v->queue, "Nocturne flip");
    if (ret < 0) goto fail;
    ret = sceVideoOutAddFlipEvent(v->queue, v->handle, NULL);
    if (ret < 0) goto fail;
    ret = sceVideoOutSetFlipRate(v->handle, ORBIS_VIDEO_OUT_FLIP_60HZ);
    if (ret < 0) goto fail;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ABGR8888;
    mode.w = 640;
    mode.h = 480;
    mode.refresh_rate = 60;
    if (SDL_AddBasicVideoDisplay(&mode) < 0) { PS4_VideoQuit(_this); return -1; }
    SDL_AddDisplayMode(&_this->displays[0], &mode);
    /* Force SDL's CPU renderer, including when no caller supplied a hint. */
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    return 0;
fail:
    PS4_VideoQuit(_this);
    return ps4_error("VideoOut initialization", ret);
}

static int PS4_CreateWindow(_THIS, SDL_Window *window)
{
    PS4_Video *v = _this->driverdata;
    if (v->window) return SDL_SetError("PS4 supports one game window");
    if (window->flags & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN | SDL_WINDOW_METAL))
        return SDL_SetError("PS4 software backend does not create GPU contexts");
    v->window = window;
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);
    return 0;
}

static void PS4_DestroyFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface = window->driverdata;
    window->driverdata = NULL;
    SDL_FreeSurface(surface);
}

static int PS4_CreateFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    PS4_DestroyFramebuffer(_this, window);
    surface = SDL_CreateRGBSurfaceWithFormat(0, window->w, window->h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!surface) return -1;
    window->driverdata = surface;
    *format = surface->format->format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

static int PS4_RetireFlip(PS4_Video *v)
{
    int ret;
    if (!v->in_flight) return 0;
    /* Keep ownership on any error. SDL_RenderPresent has no return value, so
     * a later frame must finish this wait before touching either buffer. */
    for (;;) {
        OrbisKernelEvent event;
        OrbisKernelUseconds timeout = 1000000;
        int events;
        ret = sceVideoOutIsFlipPending(v->handle);
        if (ret < 0) return ps4_error("sceVideoOutIsFlipPending", ret);
        if (!ret) break;
        ret = sceKernelWaitEqueue(v->queue, &event, 1, &events, &timeout);
        if (ret < 0) return ps4_error("sceKernelWaitEqueue", ret);
    }
    v->in_flight = SDL_FALSE;
    v->current ^= 1;
    return 0;
}

static int PS4_UpdateFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int count)
{
    PS4_Video *v = _this->driverdata;
    SDL_Surface *surface = window->driverdata;
    int ret;
    (void)rects; (void)count;
    if (!surface) return SDL_SetError("No PS4 window framebuffer");
    if (PS4_RetireFlip(v) < 0) return -1;
    /* Nearest-neighbor scaling preserves the recovered rasterizer's pixels.
     * 640x480 fills 1440x1080, with equal black borders on a 16:9 display. */
    nocturne_ps4_scale_pixels(surface->pixels, surface->w, surface->h, surface->pitch,
                              v->buffers[v->current], v->width, v->height);
    ret = sceVideoOutSubmitFlip(v->handle, v->current, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    if (ret < 0) return ps4_error("sceVideoOutSubmitFlip", ret);
    v->in_flight = SDL_TRUE;
    if (PS4_RetireFlip(v) < 0) return -1;
    sceSystemServiceHideSplashScreen();
    return 0;
}

static void PS4_DestroyWindow(_THIS, SDL_Window *window)
{
    PS4_Video *v = _this->driverdata;
    PS4_DestroyFramebuffer(_this, window);
    v->window = NULL;
}
static void PS4_PumpEvents(_THIS) { (void)_this; }
static void PS4_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device->driverdata);
    SDL_free(device);
}
static SDL_VideoDevice *PS4_CreateDevice(void)
{
    SDL_VideoDevice *device = SDL_calloc(1, sizeof(*device));
    PS4_Video *v = SDL_calloc(1, sizeof(*v));
    if (!device || !v) { SDL_free(device); SDL_free(v); SDL_OutOfMemory(); return NULL; }
    v->handle = -1;
    v->registration = -1;
    v->physical = -1;
    device->driverdata = v;
    device->VideoInit = PS4_VideoInit;
    device->VideoQuit = PS4_VideoQuit;
    device->CreateSDLWindow = PS4_CreateWindow;
    device->DestroyWindow = PS4_DestroyWindow;
    device->CreateWindowFramebuffer = PS4_CreateFramebuffer;
    device->UpdateWindowFramebuffer = PS4_UpdateFramebuffer;
    device->DestroyWindowFramebuffer = PS4_DestroyFramebuffer;
    device->PumpEvents = PS4_PumpEvents;
    device->free = PS4_DeleteDevice;
    return device;
}
VideoBootStrap PS4_bootstrap = { "ps4", "PS4 software VideoOut", PS4_CreateDevice, NULL };
