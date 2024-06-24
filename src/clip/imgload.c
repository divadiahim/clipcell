#include "imgload.h"

static void read_data_memory(png_structp png_ptr, png_bytep data, png_size_t length) {
   MEMORY_READER_STATE *f = png_get_io_ptr(png_ptr);
   if (length > (f->bufsize - f->current_pos))
      png_error(png_ptr, "read error in read_data_memory (loadpng)");
   memcpy(data, f->buffer + f->current_pos, length);
   f->current_pos += length;
}

void get_buf_from_png(Image *image, void *data, uint32_t size) {
   memset(image, 0, sizeof(Image));
   png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if (!png) abort();
   png_infop info = png_create_info_struct(png);
   if (!info) abort();
   if (setjmp(png_jmpbuf(png))) abort();

   MEMORY_READER_STATE memory_reader_state;
   memory_reader_state.buffer = (png_bytep)data;
   memory_reader_state.bufsize = size;
   memory_reader_state.current_pos = 0;
   png_set_read_fn(png, &memory_reader_state, read_data_memory);
   png_read_info(png, info);

   image->width = png_get_image_width(png, info);
   image->height = png_get_image_height(png, info);
   int color_type = png_get_color_type(png, info);
   int bit_depth = png_get_bit_depth(png, info);

   if (bit_depth == 16)
      png_set_strip_16(png);

   if (color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_palette_to_rgb(png);

   // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
   if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
      png_set_expand_gray_1_2_4_to_8(png);

   if (png_get_valid(png, info, PNG_INFO_tRNS))
      png_set_tRNS_to_alpha(png);

   // These color_type don't have an alpha channel then fill it with 0xff.
   if (color_type == PNG_COLOR_TYPE_RGB ||
       color_type == PNG_COLOR_TYPE_GRAY ||
       color_type == PNG_COLOR_TYPE_PALETTE)
      png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

   if (color_type == PNG_COLOR_TYPE_GRAY ||
       color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
      png_set_gray_to_rgb(png);

   png_read_update_info(png, info);

   png_bytep *row_pointers = NULL;
   if (row_pointers) abort();

   row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * image->height);
   for (int y = 0; y < image->height; y++) {
      row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png, info));
   }

   png_read_image(png, row_pointers);
   png_destroy_read_struct(&png, &info, NULL);
   image->data = row_pointers;
   printf("Image loaded\n");
}
