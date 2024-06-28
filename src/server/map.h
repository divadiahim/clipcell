#pragma once
#include <errno.h>
#include <fcntl.h>
#include <magic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum mime_ {
   MIME_TEXT,
   MIME_IMAGE_PNG,
   MIME_IMAGE_OTHER,
   MIME_AUDIO,
   MIME_VIDEO,
   MIME_APPLICATION,
   MIME_UNKNOWN
} mime_t;

typedef struct node_t {
   uint32_t next;
   uint32_t size;
} Node;

typedef struct entry_t {
   void* data;
   uint32_t size;
   mime_t mime;
   char *mime_desc;
} Entry;

void pushNode(void* list, void* buf, size_t bufsize, uint32_t* head);
void newNode(void* list, void* buf, size_t bufsize);
int get_enr(void* data);
Entry* get_entries(void* list, uint32_t head, magic_t* magic, int count);
void mimeInit(magic_t* magic);
void mimeClose(magic_t* magic);
char* getMimeDesc(magic_t* magic, void* data, uint32_t size);
mime_t getMime(const char *mime);
