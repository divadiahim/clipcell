/*
File for color and positioning functions
*/
#include "util.h"

Rect rects[TOTAL_RECTS];
Rect textmap[TOTAL_RECTS];
FT_Library library;
uint64_t colorsGamma[TOTAL_COLORS] = {0};
const char exclchars[] = {'\n', '\t', '\r', '\v', '\f', '\0'};

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

int32_t cshmf(uint32_t size) {
   char fnm[] = "clipcell-000000";

   int32_t fd = 0;
   uint32_t retries = 100;
   do {
      randname(fnm);
      fd = shm_open(fnm, O_RDWR | O_CREAT | O_EXCL, 0600);
      if (fd >= 0) {
         break;
      }
   } while (--retries);
   if (retries == 0) {
      LOG(0, "Could not create the shm file! [%m]\n");
      exit(1);
   }
   ERRCHECK(!shm_unlink(fnm), "Could not unlink the shm file!");
   ERRCHECK(!ftruncate(fd, size), "Could not truncate the shm file!");
   return fd;
}

void *open_shm_file_data(char *name, int *tfd) {
   *tfd = shm_open(name, O_RDWR, 0600);
   if (*tfd < 0)
      return NULL;
   struct stat st;
   if (fstat(*tfd, &st) < 0) {
      close(*tfd);
      return NULL;
   }
   void *data = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, *tfd, 0);
   if (data == MAP_FAILED) {
      close(*tfd);
      return NULL;
   }
   return data;
}
void truncate_shm_file(int fd, void *clipdata, uint32_t size) {
   struct stat st;
   if (fstat(fd, &st) < 0) {
      close(fd);
      return;
   }
   munmap(clipdata, st.st_size);
   ERRCHECK(!ftruncate(fd, st.st_size - size), "Could not truncate the shm file!");
}
Entry *build_textlist(void *data, uint16_t *enr) {
   fflush(stdout);
   magic_t magic;
   mimeInit(&magic);
   Entry *entries = get_entries(data, &magic, enr);
   magic_close(magic);
   return entries;
}

uint32_t *utf8_to_utf32(const char *utf8_str, uint32_t *out_len, uint32_t in_len) {
   uint32_t *utf32_str = NULL;
   uint32_t len = 0;
   while (*utf8_str && len < in_len) {
      if ((*utf8_str == ' ' && *(utf8_str + 1) == ' ') || strchr(exclchars, *utf8_str)) {
         utf8_str++;
         in_len--;
         continue;
      }
      uint32_t codepoint = 0;
      uint8_t c = *utf8_str++;
      if (c <= 0x7F) {
         codepoint = c;
      } else if (c <= 0xBF) {
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

void output_entry(Entry entry) {
   for (int i = 0; i < entry.size; i++) {
      printf("%c", ((char *)entry.data)[i]);
   }
}

void free_entries(Entry *entries, int count) {
   for (int i = 0; i < count; i++) {
      free(entries[i].mime_desc);
   }
   free(entries);
}

void free_textlist(Text nntextmap[TOTAL_RECTS]) {
   for (int i = 0; i < TOTAL_RECTS; i++) {
      free_stringlist(nntextmap[i].glyphs, nntextmap[i].num_glyphs);
   }
}

void free_stringlist(TGlyph *glyphs, int count) {
   for (int i = 0; i < count; i++) {
      if (glyphs[i].image != NULL)
         FT_Done_Glyph(glyphs[i].image);
   }
}

void free_imglist(Image imgmap[TOTAL_RECTS]) {
   for (int i = 0; i < TOTAL_RECTS; i++) {
      free_image(imgmap[i]);
   }
}
void free_image(Image image) {
   if (image.data == NULL)
      return;
   for (int i = 0; i < image.height; i++) {
      free(image.data[i]);
   }
   free(image.data);
}