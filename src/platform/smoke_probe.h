#pragma once
#include <cstddef>

struct SDL_Window;
struct SDL_Renderer;

// Opt-in integration instrumentation. Inert without NOCTURNE_SMOKE_DIR.
void nocturne_smoke_before_swap(SDL_Window* window);
void nocturne_smoke_before_sdl_present(SDL_Window* window, SDL_Renderer* renderer);
void nocturne_smoke_audio_queued(const void* pcm, std::size_t bytes, int rate, int channels);
