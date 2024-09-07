#pragma once
#include <stdint.h>

#define TOTAL_COLORS 5
typedef enum Colors { BACKG, FOREG, BORDER, BORDER_SELECTED, BOX } Colors;
static const uint64_t colors[TOTAL_COLORS] = { /// All colours are in ARGB format
    [BACKG] = 0xFF0e0e10, /// Background of the entire window
    [FOREG] = 0xFFc6c5cf, 
    [BORDER] = 0xFF2B2B31,
    [BORDER_SELECTED] = 0xFFc2c5dc,
    [BOX] = 0xFF1f1f23, /// Background inside an entry
};

#define TOTAL_RECTS 4 /// Number of visible entries

#define FONT_PATH "/usr/share/fonts/koruri/Koruri-Regular.ttf"
#define GAMMA 1.8f /// Gamma for text rendering
#define TEXT_SIZE 7

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define SCREEN_DIAG 15.6

#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 390

#define BOX_START 20
#define BOX_HEIGHT 80
#define BOX_PADDING 10
#define TEXT_PADDING 5
#define BOX_SPACING 8
#define BORDER_WIDTH 5
#define BORDER_RADIUS 10
#define IMAGE_HEIGHT (BOX_HEIGHT - 2 * BORDER_WIDTH - TEXT_PADDING)
#define IMAGE_WIDTH (WINDOW_WIDTH - 2 * (BOX_PADDING + BORDER_WIDTH + TEXT_PADDING))

#define MAX_GLYPHS 5000

#define DIAGONAL sqrt(SCREEN_HEIGHT * SCREEN_HEIGHT + SCREEN_WIDTH * SCREEN_WIDTH)
#define DPI (DIAGONAL / SCREEN_DIAG)

