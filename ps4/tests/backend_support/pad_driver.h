#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Physical samples enter the real PS4 driver, whose mappings feed the game. */
void ps4_test_pad_sample(uint32_t buttons);
#ifdef __cplusplus
}
#endif
