#include <stdint.h>

#include "types.h"
// gama correction

Color apply_gama(Color color) {
   // use this formula  255 * POWER(gammavalue / 255,2.2)
   color.r = 255 * pow(color.r / 255, gama);
   color.g = 255 * pow(color.g / 255, gama);
   color.b = 255 * pow(color.b / 255, gama);
   return color;
}

Color apply_inverse_gama(Color color) {
   // use this formula  255*POWER(linearvalue/255,1/2.2)
   color.r = 255 * pow(color.r / 255, 1.0f / gama);
   color.g = 255 * pow(color.g / 255, 1.0f / gama);
   color.b = 255 * pow(color.b / 255, 1.0f / gama);
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
   result.a = fg.a;
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

static int create_shm_file(void) {
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
/*wayland*/
struct my_state {
   struct wl_display *display;
   struct wl_output *output;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   struct zwlr_layer_shell_v1 *layer_shell;
   struct zwlr_layer_surface_v1 *layer_surface;
   struct wl_shm *shm;
   struct xdg_wm_base *xdg_wm_base;
   struct xdg_surface *xdg_surface;
   struct xdg_toplevel *xdg_toplevel;
   struct xdg_popup *xdg_popup;
   struct xdg_positioner *xdg_positioner;
   struct wl_seat *wl_seat;
   struct wl_keyboard *wl_keyboard;
   struct wl_pointer *wl_pointer;
   struct pointer_event pointer_event;
   struct xkb_state *xkb_state;
   struct xkb_context *xkb_context;
   struct xkb_keymap *xkb_keymap;
   struct repeatInfo rinfo;
   __timer_t timer;
   uint32_t width, height;
   bool closed;
   FT_Face face;
   Color currColor;
   rectC rectsC[TOTAL_RECTS];
};
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
   /* Sent by the compositor when it's no longer using this buffer */
   wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

void draw_pixel(int x, int y, float intensity, struct my_state *state, void *data) {
   if (x < 0 || x >= state->width || y < 0 || y >= state->height) {
      return;
   }
   uint32_t *pixel = &((uint32_t *)data)[y * state->width + x];
   Color bg = getColorFromHex(*pixel);
   bg = apply_gama(bg);
   Color result = blend(state->currColor, bg, 1 - intensity);
   result = apply_inverse_gama(result);
   *pixel = getColorHex(result);
}
void setPixelAA(int x, int y, int c, struct my_state *state, void *data) {
   draw_pixel(x, y, c / 255.0, state, data);
}
void setPixelColor(int x, int y, int c, struct my_state *state, void *data) {
   draw_pixel(x, y, (c & 0x000000ff) / 255.0, state, data);
}
int max(int a, int b) { return a > b ? a : b; }
void plotLineAA(int x0, int y0, int x1, int y1, struct my_state *state, void *data, int wd) {
   int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
   int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
   int err = dx - dy, e2, x2, y2; /* error value e_xy */
   float ed = dx + dy == 0 ? 1 : sqrt((float)dx * dx + (float)dy * dy);
   for (wd = (wd + 1) / 2;;) { /* pixel loop */
      setPixelColor(x0, y0, max(0, 255 * (abs(err - dx + dy) / ed - wd + 1)), state, data);
      e2 = err;
      x2 = x0;
      if (2 * e2 >= -dx) { /* x step */
         for (e2 += dy, y2 = y0; e2 < ed * wd && (y1 != y2 || dx > dy); e2 += dx)
            setPixelColor(x0, y2 += sy, max(0, 255 * (abs(e2) / ed - wd + 1)), state, data);
         if (x0 == x1) break;
         e2 = err;
         err -= dy;
         x0 += sx;
      }
      if (2 * e2 <= dy) { /* y step */
         for (e2 = dx - e2; e2 < ed * wd && (x1 != x2 || dx < dy); e2 += dy)
            setPixelColor(x2 += sx, y0, max(0, 255 * (abs(e2) / ed - wd + 1)), state, data);
         if (y0 == y1) break;
         err += dx;
         y0 += sy;
      }
   }
}
void plotQuadRationalBezierSegAA(int x0, int y0, int x1, int y1,
                                 int x2, int y2, float w, bool aa, struct my_state *state, void *data) { /* draw an anti-aliased rational quadratic Bezier segment, squared weight */
   int sx = x2 - x1, sy = y2 - y1;                                                                       /* relative values for checks */
   double dx = x0 - x2, dy = y0 - y2, xx = x0 - x1, yy = y0 - y1;
   double xy = xx * sy + yy * sx, cur = xx * sy - yy * sx, err, ed; /* curvature */
   bool f;

   // assert(xx * sx <= 0.0 && yy * sy <= 0.0); /* sign of gradient must not change */

   if (cur != 0.0 && w > 0.0) {                                /* no straight line */
      if (sx * (long)sx + sy * (long)sy > xx * xx + yy * yy) { /* begin with longer part */
         x2 = x0;
         x0 -= dx;
         y2 = y0;
         y0 -= dy;
         cur = -cur; /* swap P0 P2 */
      }
      xx = 2.0 * (4.0 * w * sx * xx + dx * dx); /* differences 2nd degree */
      yy = 2.0 * (4.0 * w * sy * yy + dy * dy);
      sx = x0 < x2 ? 1 : -1; /* x step direction */
      sy = y0 < y2 ? 1 : -1; /* y step direction */
      xy = -2.0 * sx * sy * (2.0 * w * xy + dx * dy);

      if (cur * sx * sy < 0) { /* negated curvature? */
         xx = -xx;
         yy = -yy;
         cur = -cur;
         xy = -xy;
      }
      dx = 4.0 * w * (x1 - x0) * sy * cur + xx / 2.0 + xy; /* differences 1st degree */
      dy = 4.0 * w * (y0 - y1) * sx * cur + yy / 2.0 + xy;

      if (w < 0.5 && dy > dx) { /* flat ellipse, algorithm fails */
         cur = (w + 1.0) / 2.0;
         w = sqrt(w);
         xy = 1.0 / (w + 1.0);
         sx = floor((x0 + 2.0 * w * x1 + x2) * xy / 2.0 + 0.5); /* subdivide curve in half  */
         sy = floor((y0 + 2.0 * w * y1 + y2) * xy / 2.0 + 0.5);
         dx = floor((w * x1 + x0) * xy + 0.5);
         dy = floor((y1 * w + y0) * xy + 0.5);
         plotQuadRationalBezierSegAA(x0, y0, dx, dy, sx, sy, cur, aa, state, data); /* plot apart */
         dx = floor((w * x1 + x2) * xy + 0.5);
         dy = floor((y1 * w + y2) * xy + 0.5);
         return plotQuadRationalBezierSegAA(sx, sy, dx, dy, x2, y2, cur, aa, state, data);
      }
      err = dx + dy - xy; /* error 1st step */
      do {                /* pixel loop */
         cur = fmin(dx - xy, xy - dy);
         ed = fmax(dx - xy, xy - dy);
         ed += 2 * ed * cur * cur / (4. * ed * ed + cur * cur); /* approximate error distance */
         x1 = 255 * fabs(err - dx - dy + xy) / ed;              /* get blend value by pixel error */
         if (x1 < 256) {
            if (aa)
               setPixelAA(x0, y0, x1, state, data);
            else
               draw_pixel(x0, y0, 0, state, data);
         }                                         /* plot curve */
         if (f = 2 * err + dy < 0) {               /* y step */
            if (y0 == y2) return;                  /* last pixel -> curve finished */
            if (dx - err < ed) {
               if (aa)
                  setPixelAA(x0 + sx, y0, 255 * fabs(dx - err) / ed, state, data);
               else
                  draw_pixel(x0 + sx, y0, 0, state, data);
            }
         }
         if (2 * err + dx > 0) {  /* x step */
            if (x0 == x2) return; /* last pixel -> curve finished */
            if (err - dy < ed)
               if (aa)
                  setPixelAA(x0, y0 + sy, 255 * fabs(err - dy) / ed, state, data);
               else
                  draw_pixel(x0, y0 + sy, 0, state, data);
            x0 += sx;
            dx += xy;
            err += dy += yy;
         }
         if (f) {
            y0 += sy;
            dy += xy;
            err += dx += xx;
         }               /* y step */
      } while (dy < dx); /* gradient negates -> algorithm fails */
   }
   // plotLineAA(x0, y0, x2, y2, state, data, 1); /* plot remaining needle to end */
}
void compute_string_bbox(FT_BBox *abbox, FT_UInt num_glyphs, TGlyph *glyphs) {
   FT_BBox bbox;

   bbox.xMin = bbox.yMin = 320000;
   bbox.xMax = bbox.yMax = -320000;

   for (int n = 0; n < num_glyphs; n++) {
      FT_BBox glyph_bbox;

      FT_Glyph_Get_CBox(glyphs[n].image, ft_glyph_bbox_pixels,
                        &glyph_bbox);

      glyph_bbox.xMin += glyphs[n].pos.x;
      glyph_bbox.xMax += glyphs[n].pos.x;
      glyph_bbox.yMin += glyphs[n].pos.y;
      glyph_bbox.yMax += glyphs[n].pos.y;

      if (glyph_bbox.xMin < bbox.xMin)
         bbox.xMin = glyph_bbox.xMin;

      if (glyph_bbox.yMin < bbox.yMin)
         bbox.yMin = glyph_bbox.yMin;

      if (glyph_bbox.xMax > bbox.xMax)
         bbox.xMax = glyph_bbox.xMax;

      if (glyph_bbox.yMax > bbox.yMax)
         bbox.yMax = glyph_bbox.yMax;
   }

   if (bbox.xMin > bbox.xMax) {
      bbox.xMin = 0;
      bbox.yMin = 0;
      bbox.xMax = 0;
      bbox.yMax = 0;
   }

   *abbox = bbox;
}
void render_text(Text *text, FT_Face face, char *str) {
   int pen_x = 0, pen_y = 0;
   PGlyph glyph;
   glyph = text->glyphs;
   for (int i = 0; i < strlen(str); i++) {
      if (pen_x >= text->rect.size.x && str[i] == ' ') {
         continue;     
      }
      FTCHECK(FT_Load_Glyph(face, FT_Get_Char_Index(face, str[i]), 0), "Failed to load glyph");
      FTCHECK(FT_Get_Glyph(face->glyph, &glyph->image), "Failed to get glyph");
      FT_Glyph_Transform(glyph->image, 0, &glyph->pos);
      glyph->pos.x = pen_x;
      if (pen_x >= text->rect.size.x) {
         pen_y += face->size->metrics.height >> 6;
         pen_x = 0;
      }
      if (pen_y >= text->rect.size.y) {
         break;
      }
      glyph->pos.y = pen_y;
      pen_x += face->glyph->advance.x >> 6;
      glyph++;
   }
   text->num_glyphs = glyph - text->glyphs;
   compute_string_bbox(&(text->string_bbox), text->num_glyphs, text->glyphs);
}
void draw_text(Text text, FT_Face face, Colors FG, Colors BG, void *data) {
   FT_Glyph image;
   FT_Vector pen;
   FT_Vector start;
   FT_BBox bbox;
   int o = 0;

   // uint32_t string_width = (text.string_bbox.xMax - text.string_bbox.xMin) / 64;
   // uint32_t string_height = (text.string_bbox.yMax - text.string_bbox.yMin) / 64;

   start.x = ((text.rect.pos.x)) * 64;
   start.y = ((text.rect.pos.y) - face->size->metrics.height) / 2;
   pen = start;
   for (int n = 0; n < text.num_glyphs; n++) {
      FTCHECK(FT_Glyph_Copy(text.glyphs[n].image, &image), "copy failed");
      FT_Glyph_Get_CBox(image, ft_glyph_bbox_pixels, &bbox);
      uint32_t pozz = 0;
      if (text.glyphs[n].pos.x + pozz >= text.rect.size.x) {
         text.rect.size.x = text.glyphs[n].pos.x + pozz;
         pen.x = start.x;
         o += face->size->metrics.height >> 6;
      }
      FT_Glyph_Transform(image, 0, &pen);
      FTCHECK(FT_Glyph_To_Bitmap(&image, FT_RENDER_MODE_LCD, 0, 1), "rendering failed");
      FT_BitmapGlyph bit = (FT_BitmapGlyph)image;
      if (o  > text.rect.size.y) {
         return;
      }
      int lcd_ww = bit->bitmap.width / 3;
      int k = 0;
      int z = -(bit->bitmap.pitch - bit->bitmap.width);
      int rr = bit->bitmap.rows;
      Color fg = getColorFromHex(colorsGamma[FG]);
      Color bg = getColorFromHex(colorsGamma[BG]);
      for (int i = 0; i < rr * lcd_ww; i++, z += 3) {
         int p = i % lcd_ww;
         Color result = blendLCD(fg, bg, bit, z);
         result = apply_inverse_gama(result);
         if (i % lcd_ww == 0) {
            k++;
            z += bit->bitmap.pitch - bit->bitmap.width;
         }
         ((uint32_t *)data)[p + (o + k + text.rect.pos.y - bit->top) * WINDOW_WIDTH +
                            bit->left] = getColorHex(result);
      }
      pen.x += image->advance.x >> 10;
      pen.y += image->advance.y >> 10;
      FT_Done_Glyph(image);
   }
}

void fillRect(int x, int y, int width, int height, struct my_state *state,
              void *data) {
   for (int i = y; i <= y + height; i++) {
      for (int j = x; j <= x + width; j++) {
         draw_pixel(j, i, 0, state, data);
      }
   }
}
void drawRoundedRectFilled(int x1, int y1, int x2, int y2, int radius, struct my_state *state, void *data) {  // draw a rounded rectangle with  Bresenham's algorithm and fill it
   int x, y;
   if (x1 > x2) {
      x = x1;
      x1 = x2;
      x2 = x;
   }
   if (y1 > y2) {
      y = y1;
      y1 = y2;
      y2 = y;
   }
   int cx1 = x1 + radius;
   int cx2 = x2 - radius;
   int cy1 = y1 + radius;
   int cy2 = y2 - radius;
   fillRect(x1, cy1, x2 - x1, cy2 - cy1 + 2, state, data);
   int r = radius;
   x = r;
   y = 0;
   int dx, dy, Error = 0;
   while (y <= x) {
      dy = 1 + 2 * y;
      y++;
      Error -= dy;
      if (Error < 0) {
         dx = 1 - 2 * x;
         x--;
         Error -= dx;
      }
      plotLineAA(cx1 - x, cy1 - y, cx2 + x, cy1 - y, state, data, 1);
      // plotLineAA(cx1 - y, cy1 - x, cx2 + y, cy1 - x, state, data, 1);
      plotLineAA(cx1 - x, cy2 + y + 1, cx2 + x, cy2 + y + 1, state, data, 1);
      plotLineAA(cx1 - y, cy2 + x + 1, cx2 + y, cy2 + x + 1, state, data, 1);
   }
}
void draw_rect(Rect rect, uint16_t radius, uint16_t thickness, bool filled, Colors backg, Colors border, void *data, struct my_state *state) {
   int x = rect.pos.x;
   int y = rect.pos.y;
   int width = rect.size.x;
   int height = rect.size.y;

   if (filled && radius == 0) {
      state->currColor = getColorFromHex(colorsGamma[backg]);
      fillRect(x, y, width, height, state, data);
      if (backg == border)
         return;
   }
   int x0 = x;
   int y0 = y;
   int x1 = x + width;
   int y1 = y + height;
   int f_thickness = ceil(thickness / 2.0) - 1;
   if (filled && radius > 0) {
      state->currColor = getColorFromHex(colorsGamma[backg]);
      drawRoundedRectFilled(x0 + 1, y0 - 1, x1 - 1, y1 - 1, radius - radius / 6, state, data);
   }
   state->currColor = getColorFromHex(colorsGamma[border]);
   plotLineAA(x0 + radius, y0, x1 - radius, y0, state, data, thickness);
   plotLineAA(x0 + radius, y1, x1 - radius, y1, state, data, thickness);
   plotLineAA(x0 + f_thickness, y0 + radius - f_thickness, x0 + f_thickness, y1 - radius, state, data, thickness);
   plotLineAA(x1, y0 + radius - f_thickness, x1, y1 - radius, state, data, thickness);
   if (radius > 0) {
      plotQuadRationalBezierSegAA(x0, y0 + radius - f_thickness, x0, y0 - f_thickness, x0 + radius, y0 - f_thickness, 1, 1, state, data);
      plotQuadRationalBezierSegAA(x0 + f_thickness, y0 + radius - f_thickness, x0 + f_thickness, y0, x0 + radius, y0, 1, 1, state, data);

      plotQuadRationalBezierSegAA(x1 - f_thickness, y0 + radius - f_thickness, x1 - f_thickness, y0, x1 - radius, y0, 1, 1, state, data);
      plotQuadRationalBezierSegAA(x1, y0 + radius - f_thickness, x1, y0 - f_thickness, x1 - radius, y0 - f_thickness, 1, 1, state, data);

      plotQuadRationalBezierSegAA(x0 + f_thickness, y1 - radius, x0 + f_thickness, y1 - f_thickness, x0 + radius, y1 - f_thickness, 1, 1, state, data);
      plotQuadRationalBezierSegAA(x0, y1 - radius, x0, y1, x0 + radius, y1, 1, 1, state, data);

      plotQuadRationalBezierSegAA(x1 - f_thickness, y1 - radius, x1 - f_thickness, y1 - f_thickness, x1 - radius, y1 - f_thickness, 1, 1, state, data);
      plotQuadRationalBezierSegAA(x1, y1 - radius, x1, y1, x1 - radius, y1, 1, 1, state, data);

      for (int i = 1; i < f_thickness; i += 1) {
         plotQuadRationalBezierSegAA(x0 + i, y0 + radius - f_thickness, x0 + i, y0 - f_thickness + i, x0 + radius, y0 - f_thickness + i, 1, 0, state, data);
         plotQuadRationalBezierSegAA(x1 - i, y0 + radius - f_thickness, x1 - i, y0 - f_thickness + i, x1 - radius, y0 - f_thickness + i, 1, 0, state, data);
         plotQuadRationalBezierSegAA(x0 + i, y1 - radius, x0 + i, y1 - i, x0 + radius, y1 - i, 1, 0, state, data);
         plotQuadRationalBezierSegAA(x1 - i, y1 - radius, x1 - i, y1 - i, x1 - radius, y1 - i, 1, 0, state, data);
      }
   }
}
static struct wl_buffer *empty_buffer(struct my_state *state) {
   struct wl_shm_pool *pool;
   int width = state->width;
   int height = state->height;
   int stride = width * 4;
   int size = stride * height;
   int fd = allocate_shm_file(size);
   if (fd < 0) {
      fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
      exit(1);
   }
   void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %m\n");
      close(fd);
      exit(1);
   }
   pool = wl_shm_create_pool(state->shm, fd, size);
   struct wl_buffer *buffer = wl_shm_pool_create_buffer(
       pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
   wl_shm_pool_destroy(pool);
   close(fd);
   munmap(data, size);
   wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
   return buffer;
}
void fill_bg_no_aa(uint32_t width, uint32_t height, void *data, struct my_state *state) {
   for (int y = 0; y < state->height; ++y) {
      for (int x = 0; x < state->width; ++x) {
         ((uint32_t *)data)[y * state->width + x] = colors[BACKG];
      }
   }
}
static struct wl_buffer *draw_frame(struct my_state *state) {
   struct wl_shm_pool *pool;
   int width = state->width;
   int height = state->height;
   int stride = width * 4;
   int size = stride * height;
   static int thickness = 0;
   thickness += 1;
   int fd = allocate_shm_file(size);
   if (fd < 0) {
      fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
      exit(1);
   }
   void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %m\n");
      close(fd);
      exit(1);
   }
   pool = wl_shm_create_pool(state->shm, fd, size);
   struct wl_buffer *buffer = wl_shm_pool_create_buffer(
       pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
   wl_shm_pool_destroy(pool);
   close(fd);

   /* fill backgrond with color*/
   fill_bg_no_aa(width, height, data, state);
   // const char *init = "Choose from clipboard";
   char *tbuf = "Thank you for filling in this form about the opening symposium and your tutorial preferences. Click on the link 'see previous responses' to see a summary of the registrations. ";
   // draw the string in the top center
   for (int i = 0; i < TOTAL_RECTS; i++) {
      draw_rect(rects[i], BORDER_RADIUS, BORDER_WIDTH, true, BOX, BORDER, data, state);
   }
   Text text;
   // text.rect.pos = (Poz){20, 20};
   // text.rect.size = (Poz){140, 40};
   text.rect = textmap[0];
   render_text(&text, state->face, tbuf);
   draw_text(text, state->face, FOREG, BOX, data);

   munmap(data, size);
   wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
   return buffer;
}
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial) {
   xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void wl_seat_name(void *data, struct wl_seat *wl_seat,
                         const char *name) {
   fprintf(stderr, "seat name: %s\n", name);
}
static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                             uint32_t serial, struct wl_surface *surface,
                             wl_fixed_t surface_x, wl_fixed_t surface_y) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_ENTER;
   client_state->pointer_event.serial = serial;
   client_state->pointer_event.surface_x = surface_x,
   client_state->pointer_event.surface_y = surface_y;
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                             uint32_t serial, struct wl_surface *surface) {
   struct my_state *client_state = data;
   client_state->pointer_event.serial = serial;
   client_state->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                              uint32_t time, wl_fixed_t surface_x,
                              wl_fixed_t surface_y) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_MOTION;
   client_state->pointer_event.time = time;
   client_state->pointer_event.surface_x = surface_x,
   client_state->pointer_event.surface_y = surface_y;
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, uint32_t time, uint32_t button,
                              uint32_t state) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
   client_state->pointer_event.time = time;
   client_state->pointer_event.serial = serial;
   client_state->pointer_event.button = button,
   client_state->pointer_event.state = state;
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                            uint32_t time, uint32_t axis, wl_fixed_t value) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS;
   client_state->pointer_event.time = time;
   client_state->pointer_event.axes[axis].valid = true;
   client_state->pointer_event.axes[axis].value = value;
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                                   uint32_t axis_source) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
   client_state->pointer_event.axis_source = axis_source;
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t time, uint32_t axis) {
   struct my_state *client_state = data;
   client_state->pointer_event.time = time;
   client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
   client_state->pointer_event.axes[axis].valid = true;
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                     uint32_t axis, int32_t discrete) {
   struct my_state *client_state = data;
   client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
   client_state->pointer_event.axes[axis].valid = true;
   client_state->pointer_event.axes[axis].discrete = discrete;
}
PointerC pointer_init = {0};
static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
   struct my_state *client_state = data;
   // static bool first_frame = true;
   struct pointer_event *event = &client_state->pointer_event;
   fprintf(stderr, "pointer frame @ %d: ", event->time);

   if (event->event_mask & POINTER_EVENT_ENTER) {
      fprintf(stderr, "entered %d, %d ", wl_fixed_to_int(event->surface_x),
              wl_fixed_to_int(event->surface_y));
      pointer_init.x = wl_fixed_to_int(event->surface_x);
      pointer_init.y = wl_fixed_to_int(event->surface_y);
   }

   if (event->event_mask & POINTER_EVENT_LEAVE) {
      fprintf(stderr, "leave");
   }

   if (event->event_mask & POINTER_EVENT_MOTION) {
      fprintf(stderr, "motion %f, %f ", wl_fixed_to_double(event->surface_x),
              wl_fixed_to_double(event->surface_y));
   }

   if (event->event_mask & POINTER_EVENT_BUTTON) {
      char *state = event->state == WL_POINTER_BUTTON_STATE_RELEASED ? "released"
                                                                     : "pressed";
      fprintf(stderr, "button %d %s ", event->button, state);
   }

   uint32_t axis_events = POINTER_EVENT_AXIS | POINTER_EVENT_AXIS_SOURCE |
                          POINTER_EVENT_AXIS_STOP | POINTER_EVENT_AXIS_DISCRETE;
   char *axis_name[2] = {
       [WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
       [WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",
   };
   char *axis_source[4] = {
       [WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
       [WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
       [WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
       [WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",
   };
   if (event->event_mask & axis_events) {
      for (size_t i = 0; i < 2; ++i) {
         if (!event->axes[i].valid) {
            continue;
         }
         fprintf(stderr, "%s axis ", axis_name[i]);
         if (event->event_mask & POINTER_EVENT_AXIS) {
            fprintf(stderr, "value %f ", wl_fixed_to_double(event->axes[i].value));
         }
         if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE) {
            fprintf(stderr, "discrete %d ", event->axes[i].discrete);
         }
         if (event->event_mask & POINTER_EVENT_AXIS_SOURCE) {
            fprintf(stderr, "via %s ", axis_source[event->axis_source]);
         }
         if (event->event_mask & POINTER_EVENT_AXIS_STOP) {
            fprintf(stderr, "(stopped) ");
         }
      }
   }

   fprintf(stderr, "\n");
   memset(event, 0, sizeof(*event));
}

static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = wl_pointer_leave,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = wl_pointer_frame,
    .axis_source = wl_pointer_axis_source,
    .axis_stop = wl_pointer_axis_stop,
    .axis_discrete = wl_pointer_axis_discrete,
};

static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t format, int32_t fd, uint32_t size) {
   struct my_state *client_state = data;
   assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

   char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
   assert(map_shm != MAP_FAILED);

   struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
       client_state->xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
       XKB_KEYMAP_COMPILE_NO_FLAGS);
   munmap(map_shm, size);
   close(fd);

   struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
   xkb_keymap_unref(client_state->xkb_keymap);
   xkb_state_unref(client_state->xkb_state);
   client_state->xkb_keymap = xkb_keymap;
   client_state->xkb_state = xkb_state;
}

static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                              uint32_t serial, struct wl_surface *surface,
                              struct wl_array *keys) {
   struct my_state *client_state = data;
   fprintf(stderr, "keyboard enter; keys pressed are:\n");
   uint32_t *key;
   wl_array_for_each(key, keys) {
      char buf[128];
      xkb_keysym_t sym =
          xkb_state_key_get_one_sym(client_state->xkb_state, *key + 8);
      xkb_keysym_get_name(sym, buf, sizeof(buf));
      fprintf(stderr, "sym: %-12s (%d), ", buf, sym);
      xkb_state_key_get_utf8(client_state->xkb_state, *key + 8, buf, sizeof(buf));
      fprintf(stderr, "utf8: '%s'\n", buf);
   }
}

void handle_timer(union sigval sv) {
   struct my_state *state = sv.sival_ptr;
   if (state->rinfo.repeat_key != -1) {
      printf("repeat key\n");
      // Handle the key press event for state->repeat_key
   }
}
static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                            uint32_t serial, uint32_t time, uint32_t key,
                            uint32_t state) {
   struct my_state *client_state = data;
   char buf[128];
   uint32_t keycode = key + 8;
   xkb_keysym_t sym =
       xkb_state_key_get_one_sym(client_state->xkb_state, keycode);
   xkb_keysym_get_name(sym, buf, sizeof(buf));
   const char *action = (state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release");
   if (state == WL_KEYBOARD_KEY_STATE_PRESSED && sym == XKB_KEY_Escape) {
      client_state->closed = true;
      return;
   }
   if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      // Key was pressed
      client_state->rinfo.repeat_key = key;

      // Create a timer
      // i just found out about this it's so op
      struct sigevent sev = {.sigev_notify = SIGEV_THREAD, .sigev_notify_function = handle_timer, .sigev_value.sival_ptr = client_state};
      timer_create(CLOCK_REALTIME, &sev, &client_state->timer);

      // Start the timer
      struct itimerspec its = {.it_value.tv_sec = client_state->rinfo.delay / 1000, .it_value.tv_nsec = (client_state->rinfo.delay % 1000) * 1000000, .it_interval.tv_sec = 0, .it_interval.tv_nsec = 1000000000 / client_state->rinfo.rate};
      timer_settime(client_state->timer, 0, &its, NULL);
   } else {
      // Key was released
      client_state->rinfo.repeat_key = -1;

      // Stop and destroy the timer
      timer_delete(client_state->timer);
   }
   xkb_state_key_get_utf8(client_state->xkb_state, keycode, buf, sizeof(buf));
   fprintf(stderr, "utf8: '%s'\n", buf);
   // client_state->offset++;
   // struct wl_buffer *buffer = draw_frame(client_state);
   // wl_surface_attach(client_state->surface, buffer, 0, 0);
   // wl_surface_damage(client_state->surface, 0, 0, 1024, 1024);
   // wl_surface_commit(client_state->surface);
}
static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                              uint32_t serial, struct wl_surface *surface) {
   fprintf(stderr, "keyboard leave\n");
}

static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                                  uint32_t serial, uint32_t mods_depressed,
                                  uint32_t mods_latched, uint32_t mods_locked,
                                  uint32_t group) {
   struct my_state *client_state = data;
   xkb_state_update_mask(client_state->xkb_state, mods_depressed, mods_latched,
                         mods_locked, 0, 0, group);
}

static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                    int32_t rate, int32_t delay) {
   /* Left as an exercise for the reader */
   struct my_state *client_state = data;
   // wl
   //     printf("rate %d delay %d\n", rate, delay);
   client_state->rinfo.rate = rate;
   client_state->rinfo.delay = delay;
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
    .keymap = wl_keyboard_keymap,
    .enter = wl_keyboard_enter,
    .leave = wl_keyboard_leave,
    .key = wl_keyboard_key,
    .modifiers = wl_keyboard_modifiers,
    .repeat_info = wl_keyboard_repeat_info,
};

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat,
                                 uint32_t capabilities) {
   struct my_state *state = data;
   // printf("%s %d", "Capabil:", capabilities);
   bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
   if (have_pointer && state->wl_pointer == NULL) {
      state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
      wl_pointer_add_listener(state->wl_pointer, &wl_pointer_listener, state);
   } else if (!have_pointer && state->wl_pointer != NULL) {
      wl_pointer_release(state->wl_pointer);
      state->wl_pointer = NULL;
   }
   bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

   if (have_keyboard && state->wl_keyboard == NULL) {
      state->wl_keyboard = wl_seat_get_keyboard(state->wl_seat);
      wl_keyboard_add_listener(state->wl_keyboard, &wl_keyboard_listener, state);
   } else if (!have_keyboard && state->wl_keyboard != NULL) {
      wl_keyboard_release(state->wl_keyboard);
      state->wl_keyboard = NULL;
   }
}

static const struct wl_seat_listener wl_seat_listener = {
    .name = wl_seat_name,
    .capabilities = wl_seat_capabilities,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
   struct my_state *state = data;
   if (strcmp(interface, wl_compositor_interface.name) == 0) {
      state->compositor =
          wl_registry_bind(registry, name, &wl_compositor_interface, 4);
   } else if (strcmp(interface, wl_shm_interface.name) == 0) {
      state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
   } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
      state->xdg_wm_base =
          wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
      xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
   } else if (strcmp(interface, wl_seat_interface.name) == 0) {  //
      state->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 9);
      wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
   } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
      state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
      // printf("Got a registry event for %s id %d\n", interface, name);
   }
}
static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
   printf("Got a registry losing event for %d\n", name);
}
static void shm_handle_global(void *data, struct wl_shm *wl_shm,
                              uint32_t format) {
   printf("The supported pixel formats are 0x%08x\n", format);
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
static const struct wl_shm_listener shm_listener = {
    .format = shm_handle_global,
    // .global_remove = shm_handle_global_remove,
};
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
   struct my_state *state = data;
   zwlr_layer_surface_v1_ack_configure(surface, serial);
   state->width = width;
   state->height = height;
   struct wl_buffer *buffer = draw_frame(state);
   wl_surface_attach(state->surface, buffer, 0, 0);
   wl_surface_damage(state->surface, 0, 0, width, height);
   wl_surface_commit(state->surface);
}
static void layer_surface_configure_temp(void *data,
                                         struct zwlr_layer_surface_v1 *surface,
                                         uint32_t serial, uint32_t width, uint32_t height) {
   struct my_state *state = data;
   zwlr_layer_surface_v1_ack_configure(surface, serial);
   state->width = width;
   state->height = height;
   struct wl_buffer *buffer = empty_buffer(state);
   wl_surface_attach(state->surface, buffer, 0, 0);
   wl_surface_damage(state->surface, 0, 0, width, height);
   wl_surface_commit(state->surface);
}
static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
   struct my_state *state = data;
   state->closed = true;
}
static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};
static const struct zwlr_layer_surface_v1_listener layer_surface_listener_temp = {
    .configure = layer_surface_configure_temp,
    .closed = layer_surface_closed,
};
void setup(struct my_state *state) {
   FTCHECK(FT_Init_FreeType(&library), "initializing freetype");
   FTCHECK(FT_New_Face(library,
                       "/usr/share/fonts/TTF/JetBrainsMono-Regular_kern.ttf", 0,
                       &state->face),
           "loading font");
   uint32_t diagonal = sqrt(SCREEN_HEIGHT * SCREEN_HEIGHT + SCREEN_WIDTH * SCREEN_WIDTH);
   float dpi = diagonal / SCREEN_DIAG;
   FTCHECK(FT_Set_Char_Size(state->face, TEXT_SIZE * 64, 0, dpi, dpi), "setting font size");
   precompute_gama();
   
}
int main(int argc, char const *argv[]) {
   struct my_state state = {0};
   struct wl_display *display = wl_display_connect(NULL);
   if (!display) {
      fprintf(stderr, "Can't connect to display\n");
      return 1;
   }
   fprintf(stderr, "connected to display\n");
   struct wl_registry *registry = wl_display_get_registry(display);
   setup(&state);
   wl_registry_add_listener(registry, &registry_listener, &state);
   wl_display_roundtrip(display);
   state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
   wl_shm_add_listener(state.shm, &shm_listener, &state);
   // this is so fucking retarded i feel so ashamed doing this
   state.surface = wl_compositor_create_surface(state.compositor);
   state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell, state.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "clipcell");
   zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
   zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener_temp, &state);
   zwlr_layer_surface_v1_set_anchor(state.layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
   zwlr_layer_surface_v1_set_size(state.layer_surface, window_size.x, window_size.y);

   wl_surface_commit(state.surface);
   wl_display_dispatch(display);
   wl_display_dispatch(display);

   // destroy the surface
   zwlr_layer_surface_v1_destroy(state.layer_surface);
   wl_surface_destroy(state.surface);

   state.surface = wl_compositor_create_surface(state.compositor);
   state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell, state.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "clipcell");
   zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);
   zwlr_layer_surface_v1_set_anchor(state.layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
   zwlr_layer_surface_v1_set_size(state.layer_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
   zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
   zwlr_layer_surface_v1_set_margin(state.layer_surface, pointer_init.y, -pointer_init.x, 0, 0);
   wl_surface_commit(state.surface);
   while (wl_display_dispatch(display) != -1) {
      if (state.closed) {
         break;
      }
   }
   wl_display_disconnect(display);
   return 0;
}