#ifndef UTIL_H
#define UTIL_H
#include "types.h"

int max(int a, int b);
int mod(int a, int b);
void compute_rects(Rect rects[TOTAL_RECTS], Rect brect, Poz tmat);
Color apply_gama(Color color);
Color apply_inverse_gama(Color color);
Color getColorFromHex(uint32_t hex);
uint32_t getColorHex(Color result);
Color blend(Color fg, Color bg, float alpha);
Color blendLCD(Color fg, Color bg, FT_BitmapGlyph slot, size_t z);
void precompute_gama();
#endif