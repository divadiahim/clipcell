#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define errExit(msg)                                                           \
  do {                                                                         \
    perror(msg);                                                   printf("%d ", errno);            \
    exit(EXIT_FAILURE);                                                        \
  } while (0)
#define BUF_SIZE 4096

int main(int argc, char *argv[]) {
  int fd;
  char *name = "OS";
  void *data;

  fd = shm_open(name, O_RDWR, 0666);
  if (fd == -1) {
    errExit("shm_open");
  }

  data = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  uint32_t *tp = (uint32_t *)(data);
  printf("tp = %d\n", tp[0]);

  if (data == MAP_FAILED)
    errExit("mmap");

  printf("%s", (char *)data);
  // shm_unlink(name);
  return 0;
}
