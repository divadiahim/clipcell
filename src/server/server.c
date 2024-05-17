#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

#include "map.h"

#define errExit(msg)         \
   do {                      \
      perror(msg);           \
      printf("%d\n", errno); \
      exit(EXIT_FAILURE);    \
   } while (0)

// void client(int fd, void *data, void *infub, size_t size) {
//    if (data == MAP_FAILED)
//       errExit("mmap");
//    uint32_t *tp = (uint32_t *)data;
//    printf("Poz %d\n", tp[0]);
//    memcpy(data + sizeof(uint32_t) + tp[0], infub, size);
//    tp[0] += size * sizeof(char);
//    print_data(data + sizeof(uint32_t), tp[0]);
// }
void client(int fd, void *data, void *infub, size_t size) {
   uint32_t *tp = (uint32_t *)data;
   Node *list = data + sizeof(uint32_t);
   void *dlist = data + sizeof(uint32_t);
   if (list->size == 0) {
      printf("list is NULL\n");
      newNode(dlist, infub, size);
   } else {
      pushNode(dlist, infub, size, tp);
   }
   printList(dlist, *tp);
}

int main(int argc, char *argv[]) {
   int fd;
   struct stat param;
   char *name = "OS";
   char *path = "/dev/shm/OS";
   void *buf = malloc(10 * sizeof(char));
   void *data = NULL;
   uint64_t bufsize = 0;

   if (argc != 2) {
      exit(EXIT_FAILURE);
   }

   int rst = atoi(argv[1]);
   int new = 0;

   freopen(NULL, "rb", stdin);
   char ch;
   uint32_t size = 0;
   uint32_t pos = 0;
   while (read(STDIN_FILENO, &ch, 1) > 0) {
      if (size >= sizeof(buf)) {
         buf = realloc(buf, size * 2);
      }
      pos += sprintf(buf + pos, "%c", ch);
      size++;
   }

   fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);

   if (fd == -1) {
      if (errno == EEXIST) {
         fd = shm_open(name, O_RDWR, 0666);
      } else
         errExit("shm_open");
   } else {
      new = 1;
   }
   stat(path, &param);
   // bufsize = param.st_size + size + sizeof(List) + sizeof(Node);
   bufsize = 100;
   if (ftruncate(fd, bufsize) == -1) {
      errExit("ftruncate");
   }
   printf("length = %lu\n", bufsize);

   data = mmap(NULL, bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (new)
      memset(data, 0, bufsize);
   if (data == MAP_FAILED)
      errExit("mmap");
   (rst == 1) ? shm_unlink(name) : client(fd, data, buf, size);
   munmap(data, bufsize);
   return 0;
}
