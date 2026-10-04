#include "ime.h"
#include "ime_text.h"
#include "file_ops.h"
#include "game/gamepad.h"

#include <cstdio>
#include <SDL.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

namespace nocturne::ps4 {
namespace {
bool load_module(OrbisSysModule module) {
    return sceSysmoduleIsLoaded(module) == 0 || sceSysmoduleLoadModule(module) == 0;
}

void ascii_to_utf16(const char* source, std::uint16_t* target, std::size_t capacity) {
    std::size_t i = 0;
    if (source) {
        for (; i + 1 < capacity && source[i]; ++i) {
            const auto c = static_cast<unsigned char>(source[i]);
            target[i] = c >= 0x20 && c <= 0x7e ? c : '?';
        }
    }
    target[i] = 0;
}

bool enter_text(const char* title, const char* hint, std::uint16_t* text,
                std::size_t capacity) {
    if (!load_module(ORBIS_SYSMODULE_LIBIME) ||
        !load_module(ORBIS_SYSMODULE_IME_DIALOG)) {
        std::fprintf(nocturne_ps4_log_stream(stderr), "PS4: could not load the text input dialog\n");
        return false;
    }
    int32_t user = -1;
    if (sceUserServiceGetInitialUser(&user) < 0) {
        // SDL normally owns initialization; this also covers a prompt before
        // the first controller is opened. The service remains shared with SDL.
        sceUserServiceInitialize(nullptr);
        if (sceUserServiceGetInitialUser(&user) < 0) return false;
    }
    std::uint16_t title_text[128]{}, hint_text[128]{};
    ascii_to_utf16(title, title_text, 128);
    ascii_to_utf16(hint, hint_text, 128);
    OrbisImeDialogSetting settings{};
    settings.userId = static_cast<uint32_t>(user);
    settings.type = ORBIS_TYPE_BASIC_LATIN;
    settings.maxTextLength = static_cast<uint32_t>(capacity - 1);
    // OpenOrbis declares wchar_t pointers, but the system dialog ABI is UTF-16.
    // Keep the game's normal 32-bit wchar_t ABI and adapt only these pointers.
    settings.inputTextBuffer = reinterpret_cast<wchar_t*>(text);
    settings.title = reinterpret_cast<const wchar_t*>(title_text);
    settings.placeholder = reinterpret_cast<const wchar_t*>(hint_text);
    settings.posx = 960;
    settings.posy = 540;
    settings.horizontalAlignment = ORBIS_H_CENTER;
    settings.verticalAlignment = ORBIS_V_CENTER;
    const auto status = sceImeDialogInit(&settings, nullptr);
    if (status < 0) {
        std::fprintf(nocturne_ps4_log_stream(stderr), "PS4: text input initialization failed (%#x)\n",
                     static_cast<unsigned>(status));
        return false;
    }
    // The system draws above the last VideoOut frame. SDL polls pads on the
    // calling thread, so no game input is consumed while this dialog is active.
    while (sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_RUNNING)
        sceKernelUsleep(16000);
    OrbisDialogResult result{};
    const bool accepted = sceImeDialogGetStatus() == ORBIS_DIALOG_STATUS_STOPPED &&
        sceImeDialogGetResult(&result) >= 0 && result.endstatus == ORBIS_DIALOG_OK;
    sceImeDialogTerm();
    // Consume the dismissal button's edge while navigation is suppressed.
    // The recovered caller then clears the raw key arrays before returning.
    SDL_GameControllerUpdate();
    nocturne_gamepad_suppress_nav(1);
    nocturne_gamepad_pump();
    nocturne_gamepad_suppress_nav(0);
    return accepted;
}
}

int show_text_input(const char* title, char* output, int requested_capacity, bool prefill) {
    const auto capacity = ime_input_capacity(requested_capacity);
    if (!output || capacity == 0) return 0;
    std::uint16_t text[300]{};
    ascii_to_utf16(prefill ? output : nullptr, text, capacity);
    const char* hint = "Use letters, numbers and ASCII punctuation";
    while (enter_text(title, hint, text, capacity)) {
        if (ime_decode_ascii(text, capacity, output, capacity)) return 1;
        hint = "ASCII only: remove accented letters or unsupported symbols";
    }
    return 0;
}

int show_filename_input(const char* title, char* output,
                        const char* extension, bool prefill) {
    if (!output) return 0;
    std::uint16_t text[300]{};
    ascii_to_utf16(prefill ? output : nullptr, text, 20);
    const char* hint = "Filename: up to 19 characters, without spaces or slashes";
    while (enter_text(title, hint, text, 20)) {
        char filename[260]{};
        if (ime_decode_ascii(text, 20, filename, 20) &&
            ime_complete_filename(filename, sizeof(filename), extension)) {
            std::strcpy(output, filename);
            return 1;
        }
        hint = "Enter a nonempty ASCII filename without spaces, slashes or colons";
    }
    output[0] = '\0';
    return 0;
}
}
