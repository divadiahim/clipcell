#pragma once
#include <png.h>

#include "types.h"
typedef struct {
   png_bytep buffer;
   png_uint_32 bufsize;
   png_uint_32 current_pos;
} MEMORY_READER_STATE;

png_bytep *get_buf_from_png(void *data, uint32_t size, uint32_t *width, uint32_t *height);