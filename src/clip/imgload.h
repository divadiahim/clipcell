#pragma once

#include "types.h"
typedef struct {
   png_bytep buffer;
   png_uint_32 bufsize;
   png_uint_32 current_pos;
} MEMORY_READER_STATE;

void get_buf_from_png(Image *image, void *data, uint32_t size);