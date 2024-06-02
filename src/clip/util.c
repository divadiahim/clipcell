/*
File for color and positioning functions
*/
#include "util.h"

Rect rects[TOTAL_RECTS];
Rect textmap[TOTAL_RECTS];
FT_Library library;
uint64_t colorsGamma[TOTAL_COLORS] = {0};

int max(int a, int b) { return a > b ? a : b; }
int mod(int a, int b) { return (a % b + b) % b; }

Color apply_gama(Color color) {
   color.r = 255 * pow(color.r / 255, GAMMA);
   color.g = 255 * pow(color.g / 255, GAMMA);
   color.b = 255 * pow(color.b / 255, GAMMA);
   return color;
}

Color apply_inverse_gama(Color color) {
   color.r = 255 * pow(color.r / 255, 1.0f / GAMMA);
   color.g = 255 * pow(color.g / 255, 1.0f / GAMMA);
   color.b = 255 * pow(color.b / 255, 1.0f / GAMMA);
   return color;
}

Color getColorFromHex(uint32_t hex) {
   Color color;
   color.a = (hex >> 24) & 0xFF;
   color.r = (hex >> 16) & 0xFF;
   color.g = (hex >> 8) & 0xFF;
   color.b = hex & 0xFF;
   return color;
}

uint32_t getColorHex(Color result) {
   return ((uint8_t)result.a << 24) | ((uint8_t)result.r << 16) | ((uint8_t)result.g << 8) | (uint8_t)result.b;
}

Color blend(Color fg, Color bg, float alpha) {
   Color result;
   result.r = alpha * fg.r + (1 - alpha) * bg.r;
   result.g = alpha * fg.g + (1 - alpha) * bg.g;
   result.b = alpha * fg.b + (1 - alpha) * bg.b;
   // result.a = fg.a;
   result.a = alpha * fg.a + (1 - alpha) * bg.a;
   return result;
}

Color blendLCD(Color fg, Color bg, FT_BitmapGlyph slot, size_t z) {
   Color result;
   result.r = ((uint32_t)slot->bitmap.buffer[z] / 255.0) * fg.r +
              (1 - ((uint32_t)slot->bitmap.buffer[z]) / 255.0) * bg.r;
   result.g = ((uint32_t)slot->bitmap.buffer[z + 1] / 255.0) * fg.g +
              (1 - ((uint32_t)slot->bitmap.buffer[z + 1]) / 255.0) * bg.g;
   result.b = ((uint32_t)slot->bitmap.buffer[z + 2] / 255.0) * fg.b +
              (1 - ((uint32_t)slot->bitmap.buffer[z + 2]) / 255.0) * bg.b;
   result.a = fg.a;
   return result;
}

void precompute_gama() {
   for (size_t i = 0; i < TOTAL_COLORS; i++) {
      Color color = getColorFromHex(colors[i]);
      colorsGamma[i] = getColorHex(apply_gama(color));
   }
}

Poz translate(Poz src, Poz translation) {
   Poz result;
   result.x = src.x + translation.x;
   result.y = src.y + translation.y;
   return result;
}

void compute_rects(Rect rects[TOTAL_RECTS], Rect brect, Poz tmat) {
   rects[0] = brect;
   for (int i = 1; i < TOTAL_RECTS; i++) {
      (rects[i]).pos = translate((rects[i - 1]).pos, tmat);
      (rects[i]).size = brect.size;
   }
}