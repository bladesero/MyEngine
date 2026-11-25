#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Separate, versioned boundary: no SDL, engine or BUI instances cross it. */
enum { MYENGINE_BUI_VIEWPORT_ABI = 1 };
enum { MBUI_MOVE, MBUI_LEFT_DOWN, MBUI_LEFT_UP, MBUI_CANCEL };

typedef struct MBUIRect {
    int32_t x, y, width, height;
} MBUIRect;

typedef struct MBUIFrame {
    const uint8_t* rgba; /* Top-down, tightly packed, valid until the next call. */
    uint64_t revision;   /* Changes when pixels or dimensions change. */
    MBUIRect viewport;   /* Drawable pixels, same coordinate space as the raster. */
} MBUIFrame;

typedef struct MBUIApi {
    void* (*create)(void);
    void (*destroy)(void*);
    int32_t (*frame)(void*, int32_t width, int32_t height, float scale, MBUIFrame*);
    void (*pointer)(void*, int32_t kind, int32_t x, int32_t y);
    int32_t (*take_reset_camera)(void*);
} MBUIApi;

typedef const MBUIApi* (*MBUIGetApi)(uint32_t version);

#ifdef __cplusplus
}
#endif
