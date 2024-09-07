#include "fdm.h"
#include "imgload.h"
#include "util.h"

struct lstate {
   uint16_t currbox;
   uint16_t pagenr;
   uint16_t currtext;
   uint16_t enr;
   uint16_t old_pagenr;
};

struct maps {
   Text nntextmap[TOTAL_RECTS];
   Image imgmap[TOTAL_RECTS];
   Entry *textl;
};

struct mon {
   int32_t width, height;
   int32_t rwidth, rheight;
   struct wl_output *output;
   struct zxdg_output_v1 *xdg_output;
};

/*wayland*/
struct my_state {
   struct wl_display *display;
   struct wl_registry *registry;
   struct wl_compositor *compositor;
   struct wl_surface *surface;
   struct zwlr_layer_shell_v1 *layer_shell;
   struct zwlr_layer_surface_v1 *layer_surface;
   struct zxdg_output_manager_v1 *xdg_output_manager;
   struct wl_shm *shm;
   struct wl_seat *wl_seat;
   struct wl_keyboard *wl_keyboard;
   struct wl_pointer *wl_pointer;
   struct pointer_event pointer_event;
   struct xkb_state *xkb_state;
   struct xkb_context *xkb_context;
   struct xkb_keymap *xkb_keymap;
   struct repeatInfo rinfo;
   struct lstate lstate;
   struct maps map;
   struct wl_buffer *buffer;
   struct fdm *fdm;
   struct mon *mons;
   void *data;
   void *clipdata;
   int cfd;
   uint32_t width, height;
   uint32_t outn;
   bool closed;
   bool should_repeat;
   Color currColor;
};

FT_Face face;
// Why no NULL listeners haaaaaaaaa?
static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name) {}
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {}
static void wl_output_done(void *data, struct wl_output *wl_output) {}
static void wl_output_scale(void *data, struct wl_output *wl_output, int32_t factor) {}
static void get_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {}
static void get_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform) {}
static void zxout_done(void *d, struct zxdg_output_v1 *z) {}
static void zxout_description(void *d, struct zxdg_output_v1 *z, const char *c) {}
static void zxout_name(void *d, struct zxdg_output_v1 *z, const char *c) {}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

static void draw_pixel(int x, int y, float intensity, struct my_state *state, void *data) {
   if (x < 0 || x >= state->width || y < 0 || y >= state->height) {
      return;
   }
   uint32_t *pixel = &((uint32_t *)data)[y * state->width + x];
   Color bg = getColorFromHex(*pixel);
   bg = apply_gama(bg);
   Color result = blend(state->currColor, bg, state->currColor.a / 255.0 * (1 - intensity));
   result = apply_inverse_gama(result);
   *pixel = getColorHex(result);
}
static void setPixelAA(int x, int y, int c, struct my_state *state, void *data) {
   draw_pixel(x, y, c / 255.0, state, data);
}
static void setPixelColor(int x, int y, int c, struct my_state *state, void *data) {
   draw_pixel(x, y, (c & 0x000000ff) / 255.0, state, data);
}
static void plotLineAA(int x0, int y0, int x1, int y1, struct my_state *state, void *data, int wd) {
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
static void plotQuadRationalBezierSegAA(int x0, int y0, int x1, int y1,
                                        int x2, int y2, float w, bool aa, struct my_state *state, void *data) { /* draw an anti-aliased rational quadratic Bezier segment, squared weight */
   int sx = x2 - x1, sy = y2 - y1;                                                                              /* relative values for checks */
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
         } /* plot curve */
         if ((f = 2 * err + dy < 0)) { /* y step */
            if (y0 == y2) return;      /* last pixel -> curve finished */
            if (dx - err < ed) {
               if (aa)
                  setPixelAA(x0 + sx, y0, 255 * fabs(dx - err) / ed, state, data);
               else
                  draw_pixel(x0 + sx, y0, 0, state, data);
            }
         }
         if (2 * err + dx > 0) {  /* x step */
            if (x0 == x2) return; /* last pixel -> curve finished */
            if (err - dy < ed) {
               if (aa)
                  setPixelAA(x0, y0 + sy, 255 * fabs(err - dy) / ed, state, data);
               else
                  draw_pixel(x0, y0 + sy, 0, state, data);
            }
            x0 += sx;
            dx += xy;
            err += dy += yy;
         }
         if (f) {
            y0 += sy;
            dy += xy;
            err += dx += xx;
         } /* y step */
      } while (dy < dx); /* gradient negates -> algorithm fails */
   }
}
static void compute_string_bbox(FT_BBox *abbox, FT_UInt num_glyphs, TGlyph *glyphs) {
   FT_BBox bbox;

   bbox.xMin = bbox.yMin = 320000;
   bbox.xMax = bbox.yMax = -320000;

   for (int n = 0; n < num_glyphs; n++) {
      FT_BBox glyph_bbox;
      FT_Glyph_Get_CBox(glyphs[n].image, ft_glyph_bbox_pixels, &glyph_bbox);

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
static void load_text(Text *text, Rect crect, const char *str, uint32_t len) {
   memset(text, 0, sizeof(Text));
   char *dstr = strdup(str);
   dstr[len] = '\0';
   uint32_t wlen = mbstowcs(NULL, dstr, 0);
   wchar_t *ostr = calloc(wlen, sizeof(wchar_t));
   mbstowcs(ostr, dstr, wlen);
   uint32_t pen_x = crect.pos.x, pen_y = 0;
   PGlyph glyph = text->glyphs;
   for (uint32_t i = 0; i < wlen; i++) {
      if ((pen_x >= crect.size.x && ostr[i] == L' ') || !iswprint(ostr[i])) {
         continue;
      }
      fflush(stdout);
      FTCHECK(FT_Load_Glyph(face, FT_Get_Char_Index(face, ostr[i]), FT_LOAD_DEFAULT), "Failed to load glyph");
      FTCHECK(FT_Get_Glyph(face->glyph, &glyph->image), "Failed to get glyph");
      FT_Glyph_Transform(glyph->image, 0, &glyph->pos);
      glyph->pos.x = pen_x;
      if (pen_x >= crect.size.x) {
         pen_y += face->size->metrics.height >> 6;
         pen_x = crect.pos.x;
      }
      if (pen_y >= crect.size.y) {
         break;
      }
      glyph->pos.y = pen_y;
      pen_x += face->glyph->advance.x >> 6;
      glyph++;
   }
   text->num_glyphs = glyph - text->glyphs;
   compute_string_bbox(&(text->string_bbox), text->num_glyphs, text->glyphs);
   free(dstr);
   free(ostr);
}
static void draw_text(Text text, Rect crect, Colors FG, Colors BG, void *data) {
   FT_Glyph image;
   FT_Vector pen = {.x = crect.pos.x << 6, .y = 0};
   FT_BBox bbox;
   for (int n = 0; n < text.num_glyphs; n++) {
      FTCHECK(FT_Glyph_Copy(text.glyphs[n].image, &image), "copy failed");
      FT_Glyph_Get_CBox(image, ft_glyph_bbox_pixels, &bbox);
      pen.x = (text.glyphs[n].pos.x >= crect.size.x) ? crect.pos.x << 6 : pen.x;
      FT_Glyph_Transform(image, 0, &pen);
      FTCHECK(FT_Glyph_To_Bitmap(&image, FT_RENDER_MODE_LCD, 0, 1), "rendering failed");
      FT_BitmapGlyph bit = (FT_BitmapGlyph)image;
      uint32_t lcd_ww = bit->bitmap.width / 3;
      uint32_t rr = bit->bitmap.rows;
      uint32_t k = 0;
      int32_t z = -(bit->bitmap.pitch - bit->bitmap.width);
      Color fg = getColorFromHex(colorsGamma[FG]);
      Color bg = getColorFromHex(colorsGamma[BG]);
      for (uint32_t i = 0; i < rr * lcd_ww; i++, z += 3) {
         uint32_t p = i % lcd_ww;
         Color result = blendLCD(fg, bg, bit, z);
         result = apply_inverse_gama(result);
         if (!p) {
            k++;
            z += bit->bitmap.pitch - bit->bitmap.width;
         }
         ((uint32_t *)data)[p + (text.glyphs[n].pos.y + k + crect.pos.y - bit->top + (face->size->metrics.height >> 7)) * WINDOW_WIDTH +
                            bit->left] = getColorHex(result);
      }
      pen.x += (image->advance.x >> 10);
      pen.y += image->advance.y >> 10;
      FT_Done_Glyph(image);
   }
}

static void fillRect(int x, int y, int width, int height, struct my_state *state,
                     void *data) {
   for (int i = y; i <= y + height; i++) {
      for (int j = x; j <= x + width; j++) {
         draw_pixel(j, i, 0, state, data);
      }
   }
}
static void drawRoundedRectFilled(int x1, int y1, int x2, int y2, int radius, struct my_state *state, void *data) {
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
      plotLineAA(cx1 - x, cy2 + y + 1, cx2 + x, cy2 + y + 1, state, data, 1);
      plotLineAA(cx1 - y, cy2 + x + 1, cx2 + y, cy2 + x + 1, state, data, 1);
   }
}
static void draw_rect(Rect rect, uint16_t radius, uint16_t thickness, bool filled, Colors backg, Colors border, void *data, struct my_state *state) {
   int f_thickness = ceil(thickness / 2.0) - 1;
   int x = rect.pos.x;
   int y = rect.pos.y + f_thickness;
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

   if (filled && radius > 0) {
      state->currColor = getColorFromHex(colorsGamma[backg]);
      drawRoundedRectFilled(x0 + 1, y0 - 1, x1 - 1, y1 - 1, radius - radius / 6, state, data);
   }
   state->currColor = getColorFromHex(colorsGamma[border]);
   plotLineAA(x0 + radius + 1, y0, x1 - radius - 1, y0, state, data, thickness);
   plotLineAA(x0 + radius + 1, y1, x1 - radius - 1, y1, state, data, thickness);
   plotLineAA(x0 + f_thickness, y0 + radius - f_thickness + 1, x0 + f_thickness, y1 - radius - 1, state, data, thickness);
   plotLineAA(x1, y0 + radius - f_thickness + 1, x1, y1 - radius - 1, state, data, thickness);
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
static void fill_bg_no_aa(void *data, struct my_state *state) {
   for (int y = 0; y < state->height; ++y) {
      for (int x = 0; x < state->width; ++x) {
         ((uint32_t *)data)[y * state->width + x] = colors[BACKG];
      }
   }
}
static void create_shm_pool(struct my_state *state) {
   uint32_t width = state->width;
   uint32_t height = state->height;
   uint32_t stride = width * 4;
   uint32_t size = stride * height;
   int fd = cshmf(size);
   if (fd < 0) {
      errExit("shm_open");
   }
   struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
   state->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
   state->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (state->data == MAP_FAILED) {
      close(fd);
      errExit("mmap");
   }
   close(fd);
   wl_shm_pool_destroy(pool);
   wl_buffer_add_listener(state->buffer, &wl_buffer_listener, NULL);
}
static void draw_png_bytep(Image img, Poz pos, void *data, struct my_state *state, float ratio) {
   if (img.data == NULL)
      return;
   float x_sratio = ratio;
   float y_sratio = ratio;
   for (uint32_t y = 0, ycnt = 0; y < img.height; y++, ycnt++, y_sratio += ratio) {
      uint16_t ry = round(y_sratio - y);
      x_sratio = ratio;
      for (uint32_t x = 0, xcnt = 0; x < img.width; x++, xcnt++, x_sratio += ratio) {
         uint16_t rx = round(x_sratio - x);
         Color fg = {0};
         uint16_t yy, xx = 0;
         for (yy = 0; yy < ry && (y + yy) < img.height; yy++) {
            for (xx = 0; xx < rx && (x + xx) < img.width; xx++) {
               fg.r += img.data[y + yy][(x + xx) * 4 + 0];
               fg.g += img.data[y + yy][(x + xx) * 4 + 1];
               fg.b += img.data[y + yy][(x + xx) * 4 + 2];
               fg.a += img.data[y + yy][(x + xx) * 4 + 3];
            }
         }
         x += (xx - 1);
         fg.r /= (yy * xx);
         fg.g /= (yy * xx);
         fg.b /= (yy * xx);
         fg.a /= (yy * xx);
         uint32_t pixel = 0;
         Color bg = getColorFromHex(colors[BOX]);
         Color result = blend(fg, bg, fg.a / 255.0);
         pixel = getColorHex(result);
         ((uint32_t *)data)[(ycnt + pos.y) * state->width + (xcnt + pos.x)] = pixel;
      }
      y += (ry - 1);
   }
}

static void draw_frame(struct my_state *state) {
   fill_bg_no_aa(state->data, state);
   for (int i = 0; i < TOTAL_RECTS; i++) {
      Colors tbrcolor = (i == state->lstate.currbox) ? BORDER_SELECTED : BORDER;
      draw_rect(rects[i], BORDER_RADIUS, BORDER_WIDTH, true, BOX, tbrcolor, state->data, state);
      uint32_t ai;
      if ((ai = i + state->lstate.pagenr) < state->lstate.enr) {
         if (state->lstate.old_pagenr != state->lstate.pagenr) {
            free_stringlist(state->map.nntextmap[i].glyphs, state->map.nntextmap[i].num_glyphs);
            free_image(state->map.imgmap[i]);
            memset(&state->map.nntextmap[i], 0, sizeof(Text));
            memset(&state->map.imgmap[i], 0, sizeof(Image));
            switch (state->map.textl[ai].mime) {
               case MIME_TEXT:
                  load_text(&state->map.nntextmap[i], textmap[i], (char *)state->map.textl[ai].data, state->map.textl[ai].size);
                  break;
               case MIME_IMAGE_PNG:
                  get_buf_from_png(&state->map.imgmap[i], state->map.textl[ai].data, state->map.textl[ai].size);
                  break;
               default:
                  load_text(&state->map.nntextmap[i], textmap[i], (char *)state->map.textl[ai].mime_desc, strlen(state->map.textl[ai].mime_desc));
                  break;
            }
         }
         draw_text(state->map.nntextmap[i], textmap[i], FOREG, BOX, state->data);
         draw_png_bytep(state->map.imgmap[i], textmap[i].pos, state->data, state, calc_scaling_factor(state->map.imgmap[i]));
      }
   }
   state->lstate.old_pagenr = state->lstate.pagenr;
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
void redraw(struct my_state *state, int direction) {
   draw_frame(state);
   wl_surface_attach(state->surface, state->buffer, 0, 0);
   wl_surface_damage(state->surface, 0, 0, state->width, state->height); /// This damage could be made smarter but honestly it really doesn't hurt performance or resource in any significant manner so :3
   wl_surface_commit(state->surface);
}
void handle_key(xkb_keysym_t sym, struct my_state *state) {
   switch (sym) {
      case XKB_KEY_Up:
         if (state->lstate.currbox > 0) {
            state->lstate.currbox--;
         } else if (state->lstate.currtext > 0) {
            state->lstate.pagenr--;
         }
         if (state->lstate.currtext > 0) {
            state->lstate.currtext--;
         }
         redraw(state, 1);
         break;
      case XKB_KEY_Down:
         if (state->lstate.currbox < TOTAL_RECTS - 1)
            state->lstate.currbox++;
         else if (state->lstate.currtext < state->lstate.enr - 1)
            state->lstate.pagenr++;
         if (state->lstate.currtext < state->lstate.enr - 1)
            state->lstate.currtext++;
         redraw(state, 0);
         break;
      case XKB_KEY_Return:
         output_entry(state->map.textl[state->lstate.currtext]);
         truncate_shm_file(state->cfd, state->clipdata, removeNode(state->clipdata, state->map.textl[state->lstate.currtext].poz));
         state->closed = true;
         break;
      default:
         break;
   }
}
static bool start_repeater(struct my_state *state, xkb_keysym_t key) {
   if (state->rinfo.rate == 0)
      return true;

   struct itimerspec t = {
       .it_value = {.tv_sec = 0, .tv_nsec = state->rinfo.delay * 1000000},
       .it_interval = {.tv_sec = 0, .tv_nsec = 1000000000 / state->rinfo.rate},
   };

   if (t.it_value.tv_nsec >= 1000000000) {
      t.it_value.tv_sec += t.it_value.tv_nsec / 1000000000;
      t.it_value.tv_nsec %= 1000000000;
   }
   if (t.it_interval.tv_nsec >= 1000000000) {
      t.it_interval.tv_sec += t.it_interval.tv_nsec / 1000000000;
      t.it_interval.tv_nsec %= 1000000000;
   }
   if (timerfd_settime(state->rinfo.timerfd, 0, &t, NULL) < 0) {
      perror("timerfd_settime");
      return false;
   }

   state->rinfo.repeat_key_sym = key;
   return true;
}
static bool stop_repeater(struct my_state *state, xkb_keysym_t key) {
   if (key != -1 && key != state->rinfo.repeat_key_sym)
      return true;

   if (timerfd_settime(state->rinfo.timerfd, 0, &(struct itimerspec){{0}}, NULL) < 0) {
      perror("timerfd_settime");
      return false;
   }

   return true;
}
static bool fdm_repeat(struct fdm *fdm, int fd, int events, void *data) {
   if (events & EPOLLHUP)
      return false;

   struct my_state *state = data;
   uint64_t expiration_count;
   ssize_t ret = read(
       state->rinfo.timerfd, &expiration_count, sizeof(expiration_count));

   if (ret < 0) {
      if (errno == EAGAIN)
         return true;
      return false;
   }
   for (size_t i = 0; i < expiration_count; i++)
      handle_key(state->rinfo.repeat_key_sym, state);
   return true;
}
static bool fdm_display(struct fdm *fdm, int fd, int events, void *data) {
   struct my_state *state = data;
   if (events & EPOLLHUP)
      return false;
   if (wl_display_dispatch(state->display) == -1)
      return false;

   return true;
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
   struct repeatInfo *rinfo = &client_state->rinfo;
   if (state == WL_KEYBOARD_KEY_STATE_PRESSED && sym == XKB_KEY_Escape) {
      client_state->closed = true;
      return;
   }
   if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      rinfo->repeat_key_sym = sym;
      start_repeater(client_state, sym);
      handle_key(sym, client_state);
   } else {
      stop_repeater(client_state, sym);
   }
   xkb_state_key_get_utf8(client_state->xkb_state, keycode, buf, sizeof(buf));
}
static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                              uint32_t serial, struct wl_surface *surface) {
   fprintf(stderr, "keyboard leave\n");
   stop_repeater(data, -1);
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
   struct my_state *client_state = data;
   client_state->rinfo.rate = rate / 2;
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
      state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
   } else if (strcmp(interface, wl_shm_interface.name) == 0) {
      state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
   } else if (strcmp(interface, wl_seat_interface.name) == 0) {
      state->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 8);
      wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
   } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
      state->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
   } else if (strcmp(interface, wl_output_interface.name) == 0) {
      state->mons = realloc(state->mons, (state->outn + 1) * sizeof(struct mon));
      state->mons[state->outn++].output = wl_registry_bind(registry, name, &wl_output_interface, 3);
   } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
      state->xdg_output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 3);
   }
}
static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {}
static void shm_handle_global(void *data, struct wl_shm *wl_shm,
                              uint32_t format) {
   fprintf(stderr, "The supported pixel formats are 0x%08x\n", format);
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
static const struct wl_shm_listener shm_listener = {
    .format = shm_handle_global,
};
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
   struct my_state *state = data;
   zwlr_layer_surface_v1_ack_configure(surface, serial);
   state->width = width;
   state->height = height;
   draw_frame(state);
   wl_surface_attach(state->surface, state->buffer, 0, 0);
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
   create_shm_pool(state);
   wl_surface_attach(state->surface, state->buffer, 0, 0);
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

static const struct wl_output_listener wl_output_listener = {
    .geometry = get_geometry,
    .mode = get_mode,
    .done = wl_output_done,
    .scale = wl_output_scale,
};

static void zxout_logical_position(void *d, struct zxdg_output_v1 *z, int x, int y) {
   struct my_state *state = d;
   for (int i = 0; i < state->outn; i++) {
      if (state->mons[i].xdg_output == z) {
         state->mons[i].rwidth = x;
         state->mons[i].rheight = y;
         break;
      }
   }
   fprintf(stderr, "logical position: %d %d\n", x, y);
}

static void zxout_logical_size(void *d, struct zxdg_output_v1 *z, int x, int y) {
   struct my_state *state = d;
   for (int i = 0; i < state->outn; i++) {
      if (state->mons[i].xdg_output == z) {
         state->mons[i].width = x;
         state->mons[i].height = y;
         break;
      }
   }
   fprintf(stderr, "logical size: %d %d\n", x, y);
}

static const struct zxdg_output_v1_listener zxout_listener = {
    .name = zxout_name,
    .logical_position = zxout_logical_position,
    .logical_size = zxout_logical_size,
    .done = zxout_done,
    .description = zxout_description,
};

void setup(struct my_state *state) {
   setlocale(LC_CTYPE, "");
   FTCHECK(FT_Init_FreeType(&library), "initializing freetype");
   FTCHECK(FT_New_Face(library, FONT_PATH, 0, &face),
           "loading font, have you set a valid font in src/config.h? Current path is " FONT_PATH);
   FTCHECK(FT_Set_Char_Size(face, TEXT_SIZE * 64, 0, DPI, DPI), "setting font size");
   precompute_gama();
   memset(state->map.nntextmap, 0, sizeof(Text) * TOTAL_RECTS);
   compute_rects(rects, box_base_rect, box_tr_mat);
   compute_rects(textmap, text_base_rect, text_tr_mat);
   state->clipdata = open_shm_file_data("OS", &state->cfd);
   ERRCHECK(state->clipdata, "open_shm_file_data");
   state->lstate.old_pagenr = UINT16_MAX;
   state->map.textl = build_textlist(state->clipdata, &state->lstate.enr);
   struct repeatInfo *rinfo = &state->rinfo;
   if ((rinfo->timerfd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
      fprintf(stderr, "Failed to create timerfd: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
   }
   state->fdm = fdm_init();
   fdm_add(state->fdm, rinfo->timerfd, EPOLLIN, &fdm_repeat, state);
   fdm_add(state->fdm, wl_display_get_fd(state->display), EPOLLIN, &fdm_display, state);
}
void zwlr_layer_surface_v1_init(struct my_state *state, const struct zwlr_layer_surface_v1_listener *layer_surface_listener_vlt) {
   state->surface = wl_compositor_create_surface(state->compositor);
   state->layer_surface = zwlr_layer_shell_v1_get_layer_surface(state->layer_shell, state->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "clipcell");
   zwlr_layer_surface_v1_add_listener(state->layer_surface, layer_surface_listener_vlt, state);
   zwlr_layer_surface_v1_set_anchor(state->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
   zwlr_layer_surface_v1_set_size(state->layer_surface, WINDOW_WIDTH, WINDOW_HEIGHT);
   zwlr_layer_surface_v1_set_keyboard_interactivity(state->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
}
void clean(struct my_state *state) {
   fdm_del(state->fdm, state->rinfo.timerfd);
   fdm_del(state->fdm, wl_display_get_fd(state->display));
   fdm_destroy(state->fdm);
   close(state->rinfo.timerfd);
   free_entries(state->map.textl, state->lstate.enr);
   free_textlist(state->map.nntextmap);
   free_imglist(state->map.imgmap);
   FT_Done_Face(face);
   FT_Done_FreeType(library);
   xkb_state_unref(state->xkb_state);
   xkb_keymap_unref(state->xkb_keymap);
   xkb_context_unref(state->xkb_context);
   wl_compositor_destroy(state->compositor);
   wl_shm_destroy(state->shm);
   wl_seat_destroy(state->wl_seat);
   wl_keyboard_destroy(state->wl_keyboard);
   wl_pointer_destroy(state->wl_pointer);
   zwlr_layer_surface_v1_destroy(state->layer_surface);
   zwlr_layer_shell_v1_destroy(state->layer_shell);
   wl_surface_destroy(state->surface);
   wl_buffer_destroy(state->buffer);
}
int main(int argc, char const *argv[]) {
   struct my_state state = {0};
   state.display = wl_display_connect(NULL);
   ERRCHECK(state.display, "Failed to connect to Wayland display server");
   struct wl_registry *registry = wl_display_get_registry(state.display);
   setup(&state);
   wl_registry_add_listener(registry, &registry_listener, &state);
   wl_display_roundtrip(state.display);

   for (uint8_t i = 0; i < state.outn; i++) {  // do you really have more than 255 monitors?
      wl_output_add_listener(state.mons[i].output, &wl_output_listener, &state);
      state.mons[i].xdg_output = zxdg_output_manager_v1_get_xdg_output(state.xdg_output_manager, state.mons[i].output);
      zxdg_output_v1_add_listener(state.mons[i].xdg_output, &zxout_listener, &state);
   }
   
   state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
   wl_shm_add_listener(state.shm, &shm_listener, &state);

   // this is so fucking retarded i feel so ashamed doing this
   zwlr_layer_surface_v1_init(&state, &layer_surface_listener_temp);
   wl_surface_commit(state.surface);
   wl_display_dispatch(state.display);
   wl_display_dispatch(state.display);

   zwlr_layer_surface_v1_destroy(state.layer_surface);
   wl_surface_destroy(state.surface);

   zwlr_layer_surface_v1_init(&state, &layer_surface_listener);
   for (uint8_t i = 0; i < state.outn; i++) {
      if ((-pointer_init.x + WINDOW_WIDTH) <= state.mons[i].width + state.mons[i].rwidth && pointer_init.y <= state.mons[i].height + state.mons[i].rheight) {
	fprintf(stderr, "monitor %d\n", i);
         zwlr_layer_surface_v1_set_margin(state.layer_surface, pointer_init.y + state.mons[i].rheight, -(pointer_init.x + state.mons[i].rwidth), 0, 0);
         break;
      }
   }
   wl_surface_commit(state.surface);

   while (!state.closed) {
      if (wl_display_flush(state.display) == -1) {
         break;
      }
      fdm_poll(state.fdm);
   }
   clean(&state);
   wl_registry_destroy(registry);
   wl_display_disconnect(state.display);
   return 0;
}

