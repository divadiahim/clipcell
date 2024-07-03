#include "map.h"

#define errExit(msg)         \
   do {                      \
      perror(msg);           \
      printf("%d\n", errno); \
      exit(EXIT_FAILURE);    \
   } while (0)

void client(void *data, void *infub, size_t size) {
   magic_t magic;
   mimeInit(&magic);
   void *dlist = data + sizeof(uint32_t);
   if (((Node *)dlist)->size == 0) {
      newNode(dlist, infub, size);
   } else {
      pushNode(dlist, infub, size, (uint32_t *)data);
   }
   magic_close(magic);
}

void *read_all(int fd, uint32_t *nread) {
   int bytes_read = 0, size_mult = 1;
   *nread = 0;
   char *buf = malloc(1024);
   bytes_read = read(fd, &buf[*nread], 256);
   while (bytes_read > 0) {
      *nread += bytes_read;
      if (*nread > size_mult * 512) {
         size_mult *= 2;
         buf = realloc(buf, size_mult * 1024);
      }
      bytes_read = read(fd, &buf[*nread], 256);
   }
   buf = realloc(buf, *nread);
   return (void *)buf;
}

int main(int argc, char *argv[]) {
   int fd;
   struct stat param;
   char *name = "OS";
   void *buf = NULL;
   void *data = NULL;
   uint64_t shmbufsize = 0;
   uint32_t bufsize = 0;
   uint8_t new = 0;

   if (argc != 2) {
      exit(EXIT_FAILURE);
   }
   int rst = atoi(argv[1]);

   if (rst == 0) {
      freopen(NULL, "rb", stdin);
      buf = read_all(STDIN_FILENO, &bufsize);
      if (bufsize == 0) {
         free(buf);
         exit(EXIT_FAILURE);
      }
      else {
         printf("bufsize = %d\n", bufsize);
      }
   }

   fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
   if (fd == -1) {
      if (errno == EEXIST) {
         fd = shm_open(name, O_RDWR, 0666);
      } else
         errExit("shm_open");
   } else {
      new = 1;
      ftruncate(fd, sizeof(uint32_t));
   }

   fstat(fd, &param);
   shmbufsize = param.st_size + bufsize + 2 * sizeof(uint32_t);
   if (ftruncate(fd, shmbufsize) == -1) {
      errExit("ftruncate");
   }
   printf("length = %lu\n", shmbufsize);

   data = mmap(NULL, shmbufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED)
      errExit("mmap");
   if (new)
      memset(data, 0, shmbufsize);
   (rst == 1) ? shm_unlink(name) : client(data, buf, bufsize);
   munmap(data, shmbufsize);
   free(buf);
   return 0;
}
