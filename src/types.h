#pragma once

#include <ft2build.h>
#include <pngconf.h>
#include <xkbcommon/xkbcommon.h>

#include "../xml/xdg-output.h"
#include "../xml/xdg-shell-client-protocol.h"
#include "../xml/zwlr-protocol.h"
#include "config.h"
#include "macros.h"

#include FT_FREETYPE_H
#include FT_GLYPH_H

/* Primitives and positioning */
typedef struct poz_t {
    int32_t x, y;
} poz_t;

typedef struct rect_t {
    poz_t pos;
    poz_t size;
} rect_t;

/* Freetype */
typedef struct tglyph_t {
    FT_UInt index; /* glyph index                  */
    FT_Vector pos; /* glyph origin on the baseline */
    FT_Glyph image;
} tglyph_t, *pglyph_t;

typedef struct Text {
    tglyph_t glyphs[MAX_GLYPHS];
    uint32_t num_glyphs;
    FT_BBox string_bbox;
} Text;

/* Colors */
typedef struct Color {
    float r, g, b, a;
} Color;

typedef struct rects_color {
    uint32_t border;
    uint32_t background;
} rectC;

/* Keyboard */
struct repeatInfo {
    uint32_t rate;
    uint32_t delay;
    xkb_keysym_t repeat_key_sym;
    int timerfd;
};

/* Mouse */
typedef struct pointerC {
    int32_t x;
    int32_t y;
} PointerC;

enum pointer_event_mask {
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

struct pointer_event {
    uint32_t event_mask;
    wl_fixed_t surface_x, surface_y;
    uint32_t button, state;
    uint32_t time;
    uint32_t serial;
    struct {
        bool valid;
        wl_fixed_t value;
        int32_t discrete;
    } axes[2];
    uint32_t axis_source;
};

/* Image */
typedef struct image {
    uint32_t width;
    uint32_t height;
    png_bytep* data;
} Image;

/* Templates */
static const rect_t box_base_rect = {{BOX_PADDING, BOX_START}, {WINDOW_WIDTH - 2 * BOX_PADDING, BOX_HEIGHT}};
static const poz_t box_tr_mat = {0, BOX_HEIGHT + BOX_SPACING};

static const rect_t text_base_rect = {
    {BOX_PADDING + BORDER_WIDTH + TEXT_PADDING, BOX_START + BORDER_WIDTH + TEXT_PADDING},
    {WINDOW_WIDTH - 2 * BOX_PADDING - BORDER_WIDTH - TEXT_PADDING, BOX_HEIGHT - 2 * BORDER_WIDTH - 2 * TEXT_PADDING}};
static const poz_t text_tr_mat = {0, BOX_HEIGHT + BOX_SPACING};

static const char exclchars[] = {'\n', '\t', '\r', '\v', '\f', '\0'};
