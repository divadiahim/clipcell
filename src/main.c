#include "freetype/fttypes.h"
#include <stdint.h>
#define _POSIX_C_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "../include/schrift.h"
#include "../include/xdg-shell-client-protocol.h"
#include FT_FREETYPE_H
#define ALPHA 1
#define BG 0xFF000000

typedef struct Color {
  float r;
  float g;
  float b;
} Color;
// gama correction
float gama = 1.8f;
float inverse_gama = 1.8f;

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

Color blend(Color fg, Color bg, float alpha) {
  Color result;
  result.r = alpha * fg.r + (1 - alpha) * bg.r;
  result.g = alpha * fg.g + (1 - alpha) * bg.g;
  result.b = alpha * fg.b + (1 - alpha) * bg.b;
  return result;
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
// scanf("%d", &n);
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

struct my_state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_surface *surface;
  struct wl_shell *shell;
  struct wl_shm *shm;
  struct wl_shell_surface *shell_surface;
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
  int width, height;
  bool closed;
  SFT sft;
  FT_Library library;
  FT_Face face;
  int offset;
};
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
  /* Sent by the compositor when it's no longer using this buffer */
  wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

void plot_line(int x0, int y0, int x1, int y1, void *data, int width) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (1) {
    // plot the point (x0, y0)
    ((uint32_t *)data)[y0 * width + x0] = 0x00ffffff;

    if (x0 == x1 && y0 == y1)
      break;

    int e2 = 2 * error;

    if (e2 >= dy) {
      if (x0 == x1)
        break;
      error += dy;
      x0 += sx;
    }

    if (e2 <= dx) {
      if (y0 == y1)
        break;
      error += dx;
      y0 += sy;
    }
  }
}
void draw_text_buffer(struct my_state *state, void *data, char *text) {
  int num_chars = strlen(text);
  int width = state->width;

  int pen_x = 0;
  int pen_y = 0;
  FT_GlyphSlot slot = state->face->glyph;
  FT_Error error = 0;
  for (int i = 0; i < num_chars; i++) {
    /* load glyph image into the slot (erase previous one) */
    // error = FT_Load_Char(state->face, 'A', FT_LOAD_TARGET_LCD);
    if (error)
      fprintf(stderr, "sussy baka");
    error = FT_Load_Glyph(state->face, FT_Get_Char_Index(state->face, text[i]), 0);
    if (error)
      fprintf(stderr, "sussy baka 2");
    error = FT_Render_Glyph(state->face->glyph, FT_RENDER_MODE_LCD);
    if (error)
      continue; /* ignore errors */
    printf("loaded char\n");
    fflush(stdout);
    int lcd_ww = slot->bitmap.width / 3;
    printf("%d\n", lcd_ww);
    fflush(stdout);
    int k = 0;
    int z = 0;
    int rr = slot->bitmap.rows;

    Color fg = {255, 255, 255};
    Color bg = {0, 0, 0};
    fg = apply_gama(fg);
    bg = apply_gama(bg);
    for (int i = 0; i < rr * lcd_ww; i++, z += 3) {
      int p = i % lcd_ww;
      Color result;
      result.r =
          ((uint32_t)slot->bitmap.buffer[z] / 255.0) * fg.r +
          (1 - ((uint32_t)slot->bitmap.buffer[z]) / 255.0) * bg.r;
      result.g =
          ((uint32_t)slot->bitmap.buffer[z + 1] / 255.0) * fg.g +
          (1 - ((uint32_t)slot->bitmap.buffer[z + 1]) / 255.0) *
              bg.g;
      result.b =
          ((uint32_t)slot->bitmap.buffer[z + 2] / 255.0) * fg.b +
          (1 - ((uint32_t)slot->bitmap.buffer[z + 2]) / 255.0) *
              bg.b;
      // // aply the inverse gama correction

      result = apply_inverse_gama(result);
      if (i % lcd_ww == 0) {
        k++;
        z += slot->bitmap.pitch - slot->bitmap.width;
      }
      // ((uint32_t *)data)[(k + pen_y) * width + p] =
      // (0xFF << 24) | ((uint8_t)result.r << 16) | ((uint8_t)result.g << 8) |
      // (uint8_t)result.b;
      int32_t xoff =   slot->metrics.horiBearingX >> 6;
      int32_t yoff = -(slot->metrics.horiBearingY >> 6);
      // printf("Face height is %d:\n",);
      // print ascender and descender
      // printf("Face ascender is %d:\n", state->face->size->metrics.ascender >> 6);
      // printf("Face descender is %d:\n", state->face->size->metrics.descender >> 6);
      int yoff_base = (state->face->size->metrics.ascender >> 6) + (state->face->size->metrics.descender >> 6) + 2;
      printf("%d\n", yoff_base);
      ((uint32_t *)data)[p + (k + yoff + yoff_base) * width + pen_x + slot->bitmap_left + state->offset] =
          (0xFF << 24) | ((uint8_t)result.r << 16) | ((uint8_t)result.g << 8) |
          (uint8_t)result.b;
    }
    /* increment pen position */
    pen_x += slot->advance.x >> 6;
    //prin bearingY
    printf("bearingY %d for char %c\n", (slot->metrics.horiBearingY >> 6), text[i]);
  }
}
static struct wl_buffer *draw_frame(struct my_state *state) {
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

  /* fill backgrond with color*/
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      ((uint32_t *)data)[y * width + x] = 0xFF000000;
    }
  }
  draw_text_buffer(state, data, "Caca de vaca");
  // int offset = 0;
  // plot_line(0 + offset, 0 + offset, 500, 500, data, width);
  // plot_line(0 + offset, 0 + offset, 500, 0 + offset, data, width);
  // plot_line(500, 500, 500, 0 + offset, data, width);
  munmap(data, size);
  wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
  return buffer;
}
static void xdg_toplevel_configure(void *data,
                                   struct xdg_toplevel *xdg_toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states) {
  struct my_state *state = data;
  if (width == 0 || height == 0) {
    /* Compositor is deferring to us */
    return;
  }
  state->width = width;
  state->height = height;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
  struct my_state *state = data;
  state->closed = true;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
  struct my_state *state = data;
  xdg_surface_ack_configure(xdg_surface, serial);
  struct wl_buffer *buffer = draw_frame(state);
  wl_surface_attach(state->surface, buffer, 0, 0);
  wl_surface_damage(state->surface, 0, 0, 1024, 1024);
  wl_surface_commit(state->surface);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};
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

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
  struct my_state *client_state = data;
  struct pointer_event *event = &client_state->pointer_event;
  fprintf(stderr, "pointer frame @ %d: ", event->time);

  if (event->event_mask & POINTER_EVENT_ENTER) {
    fprintf(stderr, "entered %f, %f ", wl_fixed_to_double(event->surface_x),
            wl_fixed_to_double(event->surface_y));
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

static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                            uint32_t serial, uint32_t time, uint32_t key,
                            uint32_t state) {
  struct my_state *client_state = data;
  char buf[128];
  uint32_t keycode = key + 8;
  xkb_keysym_t sym =
      xkb_state_key_get_one_sym(client_state->xkb_state, keycode);
  xkb_keysym_get_name(sym, buf, sizeof(buf));
  const char *action =
      state == WL_KEYBOARD_KEY_STATE_PRESSED ? "press" : "release";
  fprintf(stderr, "key %s: sym: %-12s (%d), ", action, buf, sym);
  xkb_state_key_get_utf8(client_state->xkb_state, keycode, buf, sizeof(buf));
  fprintf(stderr, "utf8: '%s'\n", buf);
  client_state->offset++;
  struct wl_buffer *buffer = draw_frame(client_state);
  wl_surface_attach(client_state->surface, buffer, 0, 0);
  wl_surface_damage(client_state->surface, 0, 0, 1024, 1024);
  wl_surface_commit(client_state->surface);
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
  printf("rate %d delay %d\n", rate, delay);
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
  } else if (strcmp(interface, wl_seat_interface.name) == 0) { //
    state->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 9);
    wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
  }
  printf("Got a registry event for %s id %d\n", interface, name);
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
    //   .global_remove = shm_handle_global_remove,
};
void setup(struct my_state *state) {
  uint32_t cp = 0x1F4A9;
  FT_Error error = 0;
  FT_Init_FreeType(&state->library);
  if (error) {
    fprintf(stderr, "Can't initialize freetype\n");
    return;
  }
  error = FT_New_Face(state->library,
                      "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf", 0,
                      &state->face);
  if (error == FT_Err_Unknown_File_Format) {
    fprintf(stderr, "font file could be opened and read, but it appears that "
                    "its font format is unsupported");
  } else if (error) {
    fprintf(stderr, " another error code means that the font file could not be "
                    "opened or read, or that it is broken");
  }
  error = FT_Set_Char_Size(state->face, 64*64, 0, 96, 96);
  // if (error)
  //   fprintf(stderr, "sussy baka");
  // error = FT_Load_Glyph(state->face, FT_Get_Char_Index(state->face, 'A'), 0);
  // if (error)
  //   fprintf(stderr, "sussy baka 2");
  // error = FT_Render_Glyph(state->face->glyph, FT_RENDER_MODE_LCD);
  // if (error)
  //   fprintf(stderr, "sussy baka 3");
  // printf("sssss %u\n", state->face->glyph->bitmap.width);
}
int main(int argc, char const *argv[]) {
  struct my_state state = {0};
  state.width = 1024;
  state.height = 1024;
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

  state.surface = wl_compositor_create_surface(state.compositor);

  state.xdg_surface =
      xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
  state.xdg_positioner = xdg_wm_base_create_positioner(state.xdg_wm_base);
  xdg_positioner_set_size(state.xdg_positioner, 1280, 640);
  xdg_positioner_set_anchor_rect(state.xdg_positioner, 0, 0, 1280, 640);
  xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
  state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
  xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
  // state.xdg_popup = xdg_surface_get_popup(state.xdg_surface,  ,
  // state.xdg_positioner);
  xdg_toplevel_set_title(state.xdg_toplevel, "Hello, world!");
  wl_surface_commit(state.surface);
  state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  wl_shm_add_listener(state.shm, &shm_listener, &state);

  while (wl_display_dispatch(display) != -1) {
    // This space intentionally left blank
    printf("version %s", sft_version());
  }

  wl_display_disconnect(display);
  return 0;
}
