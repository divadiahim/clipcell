/*
File for color and positioning functions
*/
#include "util.h"

Rect rects[TOTAL_RECTS];
Rect textmap[TOTAL_RECTS];
FT_Library library;
uint64_t colorsGamma[TOTAL_COLORS] = {0};
char exclchars[] = {'\n', '\t', '\r', '\v', '\f'};

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
   result.a = bg.a;
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
      printf("Rect %d: %d %d %d %d\n", i, (rects[i]).pos.x, (rects[i]).pos.y, (rects[i]).size.x, (rects[i]).size.y);
   }
}

/*shared memory support*/
static void randname(char *buf) {
   struct timespec ts;
   clock_gettime(0, &ts);
   long r = ts.tv_nsec;
   for (int i = 0; i < 6; ++i) {
      buf[i] = 'A' + (r & 15) + (r & 16) * 2;
      r >>= 5;
   }
}

int create_shm_file() {
   int retries = 100;
   do {
      char name[] = "/wl_shm-XXXXXX";
      randname(name + sizeof(name) - 7);
      --retries;
      int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
      if (fd >= 0) {
         shm_unlink(name);
         return fd;
      }
   } while (retries > 0 && errno == EEXIST);
   return -1;
}
int allocate_shm_file(size_t size) {
   int fd = create_shm_file();
   if (fd < 0)
      return -1;
   int ret;
   do {
      ret = ftruncate(fd, size);
   } while (ret < 0 && errno == EINTR);
   if (ret < 0) {
      close(fd);
      return -1;
   }
   return fd;
}

void *open_shm_file_data(char *name) {
   int fd = shm_open(name, O_RDWR, 0600);
   if (fd < 0)
      return NULL;
   struct stat st;
   if (fstat(fd, &st) < 0) {
      close(fd);
      return NULL;
   }
   void *data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED) {
      close(fd);
      return NULL;
   }
   return data;
}

Entry *build_textlist(void *data, uint32_t size) {
   printf("Size: %d\n", 5);
   fflush(stdout);
   magic_t magic;
   mimeInit(&magic);
   void *dlist = data + sizeof(uint32_t);
   Entry *entries = get_entries(dlist, *(uint32_t *)data, &magic, size);
   mimeClose(&magic);
   return entries;
}

uint32_t *utf8_to_utf32(const char *utf8_str, uint32_t *out_len) {
   uint32_t *utf32_str = NULL;
   uint32_t len = 0;
   while (*utf8_str) {
      if ((*utf8_str == ' ' && *(utf8_str + 1) == ' ') || strchr(exclchars, *utf8_str)) {
         utf8_str++;
         continue;
      }
      uint32_t codepoint = 0;
      uint8_t c = *utf8_str++;
      if (c <= 0x7F) {
         codepoint = c;
      } else if (c <= 0xBF) {
         // Invalid byte, ignore it
         continue;
      } else if (c <= 0xDF) {
         codepoint = (c & 0x1F) << 6;
         codepoint |= (*utf8_str++ & 0x3F);
      } else if (c <= 0xEF) {
         codepoint = (c & 0x0F) << 12;
         codepoint |= (*utf8_str++ & 0x3F) << 6;
         codepoint |= (*utf8_str++ & 0x3F);
      } else if (c <= 0xF7) {
         codepoint = (c & 0x07) << 18;
         codepoint |= (*utf8_str++ & 0x3F) << 12;
         codepoint |= (*utf8_str++ & 0x3F) << 6;
         codepoint |= (*utf8_str++ & 0x3F);
      }
      utf32_str = (uint32_t *)realloc(utf32_str, (len + 1) * sizeof(uint32_t));
      utf32_str[len++] = codepoint;
   }
   *out_len = len;
   return utf32_str;
}