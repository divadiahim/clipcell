#include "map.h"

#include <stdio.h>
#include <string.h>
Node *allocNode(List *list) {
   return &list->nodes[list->npool];
   list->npool++;
}

void newNode(void *list, void *buf, size_t bufsize) {
   Node *l = (Node *)list;
   l->next = 0;
   l->size = bufsize + 2 * sizeof(uint32_t);
   l->data = list + 2 * sizeof(uint32_t);
   memcpy(l->data, buf, bufsize);
   return;
}

void pushNode(void *list, void *buf, size_t bufsize, uint32_t *head) {
   void *poz = list + *head + ((Node *)(list + *head))->size;
   Node *newnode = (Node *)poz;
   newnode->next = *head;
   newnode->size = bufsize + 2 * sizeof(uint32_t);
   newnode->data = poz + 2 * sizeof(uint32_t);
   memcpy(newnode->data, buf, bufsize);
   *head += newnode->size;
   return;
}

void printList(void *list, uint32_t head) {
   void *poz = list + head;
   Node *current = (Node *)poz;
   uint8_t exitcount;
   exitcount = head == 0 ? 1 : 2;
   do {
      if (current->next == 0) {
         exitcount--;
      }
      printf("Size: %d\n", current->size);
      printf("Next: %d\n", current->next);
      print_data(poz + 2 * sizeof(uint32_t), current->size - 2 * sizeof(uint32_t));
      printf("\n");
      poz = list + current->next;
      current = (Node *)poz;
   } while (exitcount);
   return;
}

void print_data(void *data, size_t size) {
   for (int i = 0; i < size; i++) {
      printf("%c", ((char *)data)[i]);
   }
   fflush(stdout);
}