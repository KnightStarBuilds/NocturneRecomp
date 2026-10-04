#pragma once

namespace nocturne::ps4 {
// Invoked only by existing engine text prompts. Cancellation preserves text;
// filename cancellation clears it, matching the two recovered dialog contracts.
int show_text_input(const char* title, char* output, int capacity, bool prefill);
int show_filename_input(const char* title, char* output,
                        const char* extension, bool prefill);
}
