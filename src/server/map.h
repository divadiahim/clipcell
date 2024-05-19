#pragma once
#include <stdint.h>
#include <stdio.h>
#include <magic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

typedef struct node_t {
   uint32_t next;
   uint32_t size;
} Node;

void pushNode(void* list, void* buf, size_t bufsize, uint32_t* head);
void newNode(void* list, void* buf, size_t bufsize);
void printList(void* list, uint32_t head, magic_t* magic);
void print_data(void* data, size_t size);
void mimeInit(magic_t* magic);
void mimeClose(magic_t* magic);
void getMime(magic_t* magic, void* data, size_t size);
