#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define errExit(msg)                                                           \
  do {                                                                         \
    perror(msg);                                                               \
    printf("%d\n", errno);                                                     \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

#define BUF_SIZE 4096

void client(int fd, void *data, char clipdata[BUFSIZ]) {
  if (data == MAP_FAILED)
    errExit("mmap");
  uint32_t *tp = (uint32_t *)data;
  printf("Poz %d\n", tp[0]);
  sprintf(data + sizeof(uint32_t) + tp[0], "%s", clipdata);
  printf("%s", (char *)data + sizeof(uint32_t));
  tp[0] += strlen(clipdata) * sizeof(char);
}

int main(int argc, char *argv[]) {
  int fd;
  char *name = "OS";
  char clipdata[BUFSIZ];
  void *data = NULL;

  if (argc != 2) {
    exit(EXIT_FAILURE);
  }

  int rst = atoi(argv[1]);
  int new = 0;

  fgets(clipdata, sizeof(clipdata), stdin);

  fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
  if (fd == -1) {
    if (errno == EEXIST) {
      fd = shm_open(name, O_RDWR, 0666);
    } else
      errExit("shm_open");
  } else {
    new = 1;
    if (ftruncate(fd, BUF_SIZE) == -1) {
      errExit("ftruncate");
    }
  }
  data = mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (new)
    memset(data, 0, BUF_SIZE);
  if (data == MAP_FAILED)
    errExit("mmap");
  (rst) ? shm_unlink(name) : client(fd, data, clipdata);
  return 0;
}
