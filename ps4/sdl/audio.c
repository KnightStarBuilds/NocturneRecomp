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
/* PS4 AudioOut backend for SDL 2.30.0. SDL driver interface scaffolding is
 * adapted from SDL's dummy audio driver; see LICENSE.txt. */
#include "SDL_internal.h"
#include "SDL_audio.h"
#include "audio/SDL_sysaudio.h"
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <stdlib.h>
#include "modules.h"
#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData {
    int handle;
    Uint8 *buffers;
    unsigned current;
};

static void PS4_CloseDevice(_THIS)
{
    if (!_this->hidden) return;
    if (_this->hidden->handle >= 0) {
        sceAudioOutOutput(_this->hidden->handle, NULL);
        sceAudioOutClose(_this->hidden->handle);
    }
    free(_this->hidden->buffers);
    SDL_free(_this->hidden);
    _this->hidden = NULL;
}

static int PS4_OpenDevice(_THIS, const char *name)
{
    int format;
    void *memory = NULL;
    (void)name;
    if (_this->iscapture) return SDL_SetError("PS4 audio capture is unavailable");
    _this->hidden = SDL_calloc(1, sizeof(*_this->hidden));
    if (!_this->hidden) return SDL_OutOfMemory();
    _this->hidden->handle = -1;
    /* SDL converts and resamples the application's stream to this device spec. */
    _this->spec.freq = 48000;
    _this->spec.channels = _this->spec.channels == 1 ? 1 : 2;
    _this->spec.format = _this->spec.format == AUDIO_F32SYS ? AUDIO_F32SYS : AUDIO_S16SYS;
    _this->spec.samples = SDL_clamp((_this->spec.samples + 255) & ~255, 256, 2048);
    SDL_CalculateAudioSpec(&_this->spec);
    if (posix_memalign(&memory, 64, (size_t)_this->spec.size * 2) != 0) {
        PS4_CloseDevice(_this);
        return SDL_OutOfMemory();
    }
    _this->hidden->buffers = memory;
    SDL_memset(memory, _this->spec.silence, (size_t)_this->spec.size * 2);
    if (_this->spec.format == AUDIO_F32SYS)
        format = _this->spec.channels == 1 ? ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO : ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO;
    else
        format = _this->spec.channels == 1 ? ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_MONO : ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
    _this->hidden->handle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
            ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, _this->spec.samples, 48000, format);
    if (_this->hidden->handle < 0) {
        const int result = _this->hidden->handle;
        PS4_CloseDevice(_this);
        return SDL_SetError("sceAudioOutOpen failed: 0x%08x", (unsigned)result);
    }
    return 0;
}
static Uint8 *PS4_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffers + _this->hidden->current * _this->spec.size;
}
static void PS4_PlayDevice(_THIS)
{
    if (sceAudioOutOutput(_this->hidden->handle, PS4_GetDeviceBuf(_this)) < 0)
        SDL_OpenedAudioDeviceDisconnected(_this);
    _this->hidden->current ^= 1;
}
/* AudioOutOutput blocks until the previous block is consumed. Do not add SDL's
 * default timed wait as well: that would insert a second block of silence. */
static void PS4_WaitDevice(_THIS) { (void)_this; }
static SDL_bool PS4_AudioInit(SDL_AudioDriverImpl *impl)
{
    int result;
    if (ps4_load_module(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT, "AudioOut") < 0)
        return SDL_FALSE;
    result = sceAudioOutInit();
    if (result < 0 && (Uint32)result != ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT) {
        SDL_SetError("sceAudioOutInit failed: 0x%08x", (unsigned)result);
        return SDL_FALSE;
    }
    impl->OpenDevice = PS4_OpenDevice;
    impl->CloseDevice = PS4_CloseDevice;
    impl->GetDeviceBuf = PS4_GetDeviceBuf;
    impl->PlayDevice = PS4_PlayDevice;
    impl->WaitDevice = PS4_WaitDevice;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    return SDL_TRUE;
}
AudioBootStrap PS4AUDIO_bootstrap = { "ps4", "PS4 AudioOut", PS4_AudioInit, SDL_FALSE };
