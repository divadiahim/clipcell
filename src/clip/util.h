#pragma once
#include "../server/map.h"
#include "types.h"

#define errExit(msg)                 \
   do {                              \
      perror(msg);                   \
      fprintf(stderr, "%d ", errno); \
      exit(EXIT_FAILURE);            \
   } while (0)

int max(int a, int b);
int mod(int a, int b);
void compute_rects(Rect rects[TOTAL_RECTS], Rect brect, Poz tmat);
Color apply_gama(Color color);
Color apply_inverse_gama(Color color);
Color getColorFromHex(uint32_t hex);
Color blend(Color fg, Color bg, float alpha);
Color blendLCD(Color fg, Color bg, FT_BitmapGlyph slot, size_t z);
uint32_t getColorHex(Color result);
void precompute_gama();
int32_t cshmf(uint32_t size);
void *open_shm_file_data(char *name, int *tfd);
void truncate_shm_file(int fd, void *clipdata, uint32_t size);
Entry *build_textlist(void *data, uint16_t *enr);
uint32_t *utf8_to_utf32(const char *utf8_str, uint32_t *out_len, uint32_t in_len);
void output_entry(Entry entry);
void free_entries(Entry *entries, int count);
void free_textlist(Text nntextmap[TOTAL_RECTS]);
void free_stringlist(TGlyph *glyphs, int count);
void free_imglist(Image imgmap[TOTAL_RECTS]);
void free_image(Image image);