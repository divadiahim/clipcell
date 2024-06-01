#include "map.h"

void newNode(void *list, void *buf, size_t bufsize) {
   Node *l = list;
   l->next = 0;
   l->size = bufsize + sizeof(Node);
   memcpy(list + sizeof(Node), buf, bufsize);
   return;
}

void pushNode(void *list, void *buf, size_t bufsize, uint32_t *head) {
   void *poz = list + *head + ((Node *)(list + *head))->size;
   Node *newnode = poz;
   newnode->next = *head;
   newnode->size = bufsize + sizeof(Node);
   memcpy(poz + sizeof(Node), buf, bufsize);
   *head += ((Node *)(list + *head))->size;
   return;
}

void printList(void *list, uint32_t head, magic_t *magic) {
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
      getMime(magic, poz + 2 * sizeof(uint32_t), current->size - 2 * sizeof(uint32_t));
      // print_data(poz + 2 * sizeof(uint32_t), current->size - 2 * sizeof(uint32_t));
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

void mimeInit(magic_t *magic) {
   if (!(*magic = magic_open(MAGIC_MIME_TYPE))) {
      perror("failed to open magic library");
   }
   if (magic_load(*magic, NULL)) {
      perror("failed to load magic database");
   }
}

void mimeClose(magic_t *magic) {
   magic_close(*magic);
   return;
}

void getMime(magic_t *magic, void *data, size_t size) {
   const char *mime = magic_buffer(*magic, data, size);
   if (mime == NULL) {
      perror("failed to get mime type");
      return;
   }
   printf("Mime type: %s\n", mime);
   return;
}
