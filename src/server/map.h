#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct node_t {
   uint32_t next;
   uint32_t size;
   void* data;
} Node;

typedef struct list_t List;
struct list_t {
   size_t head;
   size_t npool;
   Node nodes[64];
};

Node* allocNode(List* list);
void pushNode(void* list, void* buf, size_t bufsize, uint32_t* head);
void newNode(void* list, void* buf, size_t bufsize);
void printList(void* list, uint32_t head);
void print_data(void* data, size_t size);
