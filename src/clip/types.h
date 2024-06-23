#ifndef TYPES_H
#define TYPES_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <freetype/ftglyph.h>
#include <ft2build.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "../xml/xdg-shell-client-protocol.h"
#include "../xml/zwlr-protocol.h"
#include "config.h"
#include "freetype/fttypes.h"
#include FT_FREETYPE_H

/* Primitives and positioning */
typedef struct poz {
   int32_t x;
   int32_t y;
} Poz;

typedef struct rect {
   Poz pos;
   Poz size;
} Rect;

/* Freetype */
extern FT_Library library;

typedef struct TGlyph_ {
   FT_UInt index; /* glyph index                  */
   FT_Vector pos; /* glyph origin on the baseline */
   FT_Glyph image;
} TGlyph, *PGlyph;

typedef struct Text {
   TGlyph glyphs[MAX_GLYPHS];
   uint32_t num_glyphs;
   FT_BBox string_bbox;
} Text;

/* Colors */
extern uint64_t colorsGamma[TOTAL_COLORS];

typedef struct Color {
   float r;
   float g;
   float b;
   float a;
} Color;

typedef enum Colors {
   BACKG,
   FOREG,
   BORDER,
   BORDER_SELECTED,
   BOX,
} Colors;

static const uint64_t colors[TOTAL_COLORS] = {
    [BACKG] = 0xFF0e0e10,
    [FOREG] = 0xffc6c5cf,
    [BORDER] = 0x0Fc2c5dc,
    [BORDER_SELECTED] = 0xFFc2c5dc,
    [BOX] = 0xFF1f1f23,
};

typedef struct rects_color {
   uint32_t border;
   uint32_t background;
} rectC;

/* Keyboard */
struct repeatInfo {
   uint32_t rate;
   uint32_t delay;
   uint32_t repeat_key;
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

/* Templates */
static const Rect box_base_rect = {{BOX_PADDING, BOX_START}, {WINDOW_WIDTH - 2 * BOX_PADDING, BOX_HEIGHT}};
static const Poz box_tr_mat = {0, BOX_HEIGHT + BOX_SPACING};
extern Rect rects[TOTAL_RECTS];

static const Rect text_base_rect = {{BOX_PADDING + BORDER_WIDTH + TEXT_PADDING, BOX_START + BORDER_WIDTH}, {WINDOW_WIDTH - 2 * BOX_PADDING - 2 * BORDER_WIDTH - 2 * TEXT_PADDING, BOX_HEIGHT - 2 * BORDER_WIDTH}};
static const Poz text_tr_mat = {0, BOX_HEIGHT + BOX_SPACING};
extern Rect textmap[TOTAL_RECTS];
extern char exclchars[];

/* Logging */
static const uint8_t log_level = 0;
#define LOG(imp,...) {if (log_level <= imp) {fprintf(stderr, __VA_ARGS__);}}
#define WLCHECK(x,e) {if(!(x)){LOG(10, "Error running " #x " on " __FILE__ ":" FUNNYCSTRING(__LINE__) ": " e "\n"); exit(1);}}
#define WLCHECKE(x,e) {if(!(x)){LOG(10, "Error running " #x " on " __FILE__ ":" FUNNYCSTRING(__LINE__) ": " e " [%m]\n"); exit(1);}}
#define FTCHECK(x,e) {uint32_t err=x;if(err){LOG(10, "Freetype error: %s %u:[%s]!\n",e,err,FT_Error_String(err)); exit(1);}}
#define ERRCHECK(x,e) {if(!(x)){LOG(10, "Error: %s %u:[%s]!\n",e,errno,strerror(errno)); exit(1);}}
#endif