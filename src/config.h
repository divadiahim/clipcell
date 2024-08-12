#pragma once

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

#define GAMMA 1.8f
#define TOTAL_COLORS 5
#define TOTAL_RECTS 4
#define MAX_GLYPHS 5000

#define DIAGONAL sqrt(SCREEN_HEIGHT * SCREEN_HEIGHT + SCREEN_WIDTH * SCREEN_WIDTH)
#define DPI (DIAGONAL / SCREEN_DIAG)