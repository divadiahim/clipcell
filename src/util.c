#include "util.h"

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