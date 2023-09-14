#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include "../include/xdg-shell-client-protocol.h"
#include "../include/schrift.h"
#include <ft2build.h>
#include FT_FREETYPE_H
/*shared memory support*/
static void
randname(char *buf)
{
    struct timespec ts;
    clock_gettime(0, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i)
    {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int
create_shm_file(void)
{
    int retries = 100;
    do
    {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
        {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

int allocate_shm_file(size_t size)
{
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do
    {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}
/*wayland*/
struct my_state
{
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
    SFT sft;
    FT_Library library;
    FT_Face face;
};
static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
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
        ((uint32_t *) data)[y0 * width + x0] = 0xffff0000;
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * error;
        
        if (e2 >= dy) {
            if (x0 == x1) break;
            error += dy;
            x0 += sx;
        }
        
        if (e2 <= dx) {
            if (y0 == y1) break;
            error += dx;
            y0 += sy;
        }
    }
}
static struct wl_buffer *draw_frame(struct my_state *state)
{
    
    struct wl_shm_pool *pool;
    int width = 1024;
    int height = 1024;
    int stride = width * 4;
    int size = stride * height;
    printf("size: %d\n", size);
    int fd = allocate_shm_file(size);
    if (fd < 0)
    {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
                size);
        exit(1);
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        exit(1);
    }
    pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
                                                         width, height,
                                                         stride,
                                                         WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    close(fd);
   
    //using plot_line to draw a triangle
    // static char *text = "Hello World!";
    // static uint32_t cp = 97;
    // printf("cp: %d\n", cp);
    // //cp++;
	// SFT_Glyph gid;  //  unsigned long gid;
	// if (sft_lookup(&state->sft, cp, &gid) < 0)
    //       printf("sft_lookup failed\n");

	// SFT_GMetrics mtx;
	// if (sft_gmetrics(&state->sft, gid, &mtx) < 0)
    //         printf("sft_gmetrics failed\n");
            
    // SFT_Image img = {
	// 	.width  = mtx.minWidth,
	// 	.height = mtx.minHeight,
	// };
    // img.pixels = malloc((size_t)img.width * (size_t)img.height);
    // //print image width and height
    // printf("width: %d, height: %d\n", img.width, img.height);
    // sft_render(&state->sft, gid, img);
    int ww = state->face->glyph->bitmap.width;
    static k = 0;
    int rr = state->face->glyph->bitmap.rows;
    for (int i = 0; i < rr* ww; i++)
	{
        int p = i % ww;
		if(i % ww == 0)
			k++, puts("");
		 ((uint32_t *) data)[p +  k*width] = (int32_t)state->face->glyph->bitmap.buffer[i];
        //  if(state->face->glyph->bitmap.buffer[i] < 128)
        //  printf("+");
        //  else printf("*");

    
	}
    k = 0;
     printf("%d", state->face->glyph->bitmap.width);
    // free(img.pixels);

    int offset = 110;
    plot_line(0+offset, 0+offset, 500, 500, data, width);
    plot_line(0+offset, 0+offset, 500, 0+offset, data, width);
    plot_line(500, 500, 500, 0+offset, data, width);
    munmap(data, size);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
   
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial)
{
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
                             uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version)
{
    struct my_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        state->compositor = wl_registry_bind(registry, name,
                                             &wl_compositor_interface, 4);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0)
    {
        state->shm = wl_registry_bind(registry, name,
                                      &wl_shm_interface, 1);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        state->xdg_wm_base = wl_registry_bind(registry, name,
                                              &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    }

    printf("Got a registry event for %s id %d\n", interface, name);
}
static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name)
{
    printf("Got a registry losing event for %d\n", name);
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
void setup(struct my_state *state)
{
    uint32_t cp = 0x1F4A9;
    FT_Error error;
    FT_Init_FreeType(&state->library);
    if(error)
    {
        fprintf(stderr, "Can't initialize freetype\n");
        return 1;
    }
    error = FT_New_Face(state->library, "/usr/share/fonts/TTF/SourceSansPro-Regular.ttf", 0, &state->face);
    if ( error == FT_Err_Unknown_File_Format )
    {
        fprintf(stderr, "font file could be opened and read, but it appears that its font format is unsupported");
    }   
    else if ( error )
    {
        fprintf(stderr, " another error code means that the font file could not be opened or read, or that it is broken");
    }
    error = FT_Set_Char_Size(state->face, 0, 16*128, 300, 300);
    if(error)
        fprintf(stderr, "sussy baka");
   error = FT_Load_Glyph(state->face, FT_Get_Char_Index(state->face, 0x0032), 0);    
    if(error)
        fprintf(stderr, "sussy baka 2");
    error = FT_Render_Glyph(state->face->glyph, FT_RENDER_MODE_NORMAL);
    if(error)
        fprintf(stderr, "sussy baka 3");
    printf("sssss %u\n", state->face->glyph->bitmap.pitch);
   // ft_pixel_mode_mono
    
    //setup schrift
    // state->sft.xScale = 16*4;
    // state->sft.yScale = 16*4;
    // state->sft.flags = SFT_DOWNWARD_Y;
    // state->sft.font = sft_loadfile("/usr/share/fonts/TTF/SourceSansPro-SemiBold.ttf");
    // if (state->sft.font == NULL)
	// 	printf("TTF load failed");
    // else printf("TTF load success\n");
}
int main(int argc, char const *argv[])
{
   
    struct my_state state = {0};
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
    {
        fprintf(stderr, "Can't connect to display\n");
        return 1;
    }
    fprintf(stderr, "connected to display\n");
    struct wl_registry *registry = wl_display_get_registry(display);
    setup(&state);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(display);

    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "Hello, world!");
    wl_surface_commit(state.surface);

    while (wl_display_dispatch(display) != -1)
    {
        // This space intentionally left blank
        printf("version %s", sft_version());
    }

    wl_display_disconnect(display);
    return 0;
}
