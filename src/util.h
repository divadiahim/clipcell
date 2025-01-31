#pragma once
#include "map.h"
#include "types.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MOD(a, b) (((a) % (b) + (b)) % (b))

#define errExit(msg)                 \
   do {                              \
      perror(msg);                   \
      fprintf(stderr, "%d ", errno); \
      exit(EXIT_FAILURE);            \
   } while (0)

#define INIT_RECTS(rects, brect, tmat) \
   do { \
      rects[0] = brect; \
      for (int i = 1; i < TOTAL_RECTS; i++) { \
         rects[i].pos = (Poz){rects[i - 1].pos.x + tmat.x, rects[i - 1].pos.y + tmat.y}; \
         rects[i].size = brect.size; \
      } \
   } while (0)

#define PRECOMPUTE_GAMA(colorsGamma) \
   do { \
      for (size_t i = 0; i < TOTAL_COLORS; i++) { \
         Color color = GET_COLOR_FROM_HEX(colors[i]); \
         color = APPLY_GAMA(color); \
         colorsGamma[i] = GET_COLOR_HEX(color); \
      } \
   } while (0)

#define APPLY_GAMA(color) \
   (Color){ \
      .r = 255 * pow((color).r / 255, GAMMA), \
      .g = 255 * pow((color).g / 255, GAMMA), \
      .b = 255 * pow((color).b / 255, GAMMA), \
      .a = (color).a \
   }

#define APPLY_INVERSE_GAMA(color) \
   (Color){ \
      .r = 255 * pow((color).r / 255, 1.0f / GAMMA), \
      .g = 255 * pow((color).g / 255, 1.0f / GAMMA), \
      .b = 255 * pow((color).b / 255, 1.0f / GAMMA), \
      .a = (color).a \
   }

#define GET_COLOR_FROM_HEX(hex) \
   (Color){ \
      .a = ((hex) >> 24) & 0xFF, \
      .r = ((hex) >> 16) & 0xFF, \
      .g = ((hex) >> 8) & 0xFF, \
      .b = (hex) & 0xFF \
   }

#define GET_COLOR_HEX(result) \
   (((uint8_t)(result).a << 24) | ((uint8_t)(result).r << 16) | ((uint8_t)(result).g << 8) | (uint8_t)(result).b)

#define BLEND(fg, bg, alpha) \
   (Color){ \
      .r = (alpha) * (fg).r + (1 - (alpha)) * (bg).r, \
      .g = (alpha) * (fg).g + (1 - (alpha)) * (bg).g, \
      .b = (alpha) * (fg).b + (1 - (alpha)) * (bg).b, \
      .a = (bg).a \
   }

#define BLEND_LCD(fg, bg, slot, z) \
   (Color){ \
      .r = ((uint32_t)(slot)->bitmap.buffer[z] / 255.0) * (fg).r + \
           (1 - ((uint32_t)(slot)->bitmap.buffer[z]) / 255.0) * (bg).r, \
      .g = ((uint32_t)(slot)->bitmap.buffer[z + 1] / 255.0) * (fg).g + \
           (1 - ((uint32_t)(slot)->bitmap.buffer[z + 1]) / 255.0) * (bg).g, \
      .b = ((uint32_t)(slot)->bitmap.buffer[z + 2] / 255.0) * (fg).b + \
           (1 - ((uint32_t)(slot)->bitmap.buffer[z + 2]) / 255.0) * (bg).b, \
      .a = (fg).a \
   }

int32_t cshmf(uint32_t size);
void *open_shm_file_data(char *name, int *tfd);
void truncate_shm_file(int fd, void *clipdata, uint32_t size);
Entry *build_textlist(void *data, uint16_t *enr);
void output_entry(Entry entry);
void free_entries(Entry *entries, int count);
void free_textlist(Text nntextmap[TOTAL_RECTS]);
void free_stringlist(TGlyph *glyphs, int count);
void free_imglist(Image imgmap[TOTAL_RECTS]);
void free_image(Image image);