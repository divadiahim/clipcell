// --colors in hex rrggbb
#ifndef CONFIG_H
#define CONFIG_H

#define TEXT_SIZE 7
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define SCREEN_DIAG 15.6

#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 420

#define BOX_START 50
#define BOX_HEIGHT 75
#define BOX_PADDING 10
#define TEXT_PADDING 1
#define BOX_SPACING 5
#define BORDER_WIDTH 5
#define BORDER_RADIUS 10

#define GAMMA 1.8f
#define TOTAL_COLORS 5
#define TOTAL_RECTS 4
#define MAX_GLYPHS 5000

#define DIAGONAL sqrt(SCREEN_HEIGHT * SCREEN_HEIGHT + SCREEN_WIDTH * SCREEN_WIDTH)
#define DPI (DIAGONAL / SCREEN_DIAG)

#endif