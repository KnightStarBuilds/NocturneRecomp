#pragma once
/* Only fields read by the production backend are modeled on the host. */
#include <stdint.h>
#define ORBIS_PAD_PORT_TYPE_STANDARD 0
enum {
    ORBIS_PAD_BUTTON_L3 = 0x0002, ORBIS_PAD_BUTTON_R3 = 0x0004,
    ORBIS_PAD_BUTTON_OPTIONS = 0x0008, ORBIS_PAD_BUTTON_UP = 0x0010,
    ORBIS_PAD_BUTTON_RIGHT = 0x0020, ORBIS_PAD_BUTTON_DOWN = 0x0040,
    ORBIS_PAD_BUTTON_LEFT = 0x0080, ORBIS_PAD_BUTTON_L1 = 0x0400,
    ORBIS_PAD_BUTTON_R1 = 0x0800, ORBIS_PAD_BUTTON_TRIANGLE = 0x1000,
    ORBIS_PAD_BUTTON_CIRCLE = 0x2000, ORBIS_PAD_BUTTON_CROSS = 0x4000,
    ORBIS_PAD_BUTTON_SQUARE = 0x8000, ORBIS_PAD_BUTTON_TOUCH_PAD = 0x100000
};
typedef struct { uint8_t x, y; } OrbisPadStick;
typedef struct {
    uint32_t buttons;
    OrbisPadStick leftStick, rightStick;
    struct { uint8_t l2, r2; } analogButtons;
    uint8_t connected;
} OrbisPadData;
typedef struct { uint8_t largeMotor, smallMotor; } OrbisPadVibeParam;
int32_t scePadInit(void);
int32_t scePadOpen(int32_t, int32_t, int32_t, void *);
int32_t scePadClose(int32_t);
int32_t scePadReadState(int32_t, OrbisPadData *);
int32_t scePadSetVibration(int32_t, const OrbisPadVibeParam *);
