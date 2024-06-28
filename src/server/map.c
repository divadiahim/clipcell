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

int get_enr(void *data) {
   void *list = data + sizeof(uint32_t);
   uint32_t head = *(uint32_t *)data;
   int count = 0;
   if (list == NULL) {
      return 0;
   }
   void *poz = list + head;
   Node *current = (Node *)poz;
   uint8_t exitcount;
   exitcount = head == 0 ? 1 : 2;

   do {
      if (current->next == 0) {
         exitcount--;
      }
      count++;
      poz = list + current->next;
      current = (Node *)poz;
   } while (exitcount);
   return count;
}

Entry *get_entries(void *list, uint32_t head, magic_t *magic, int count) {
   if (count == 0) {
      return NULL;
   }
   Entry *entries = malloc(count * sizeof(Entry));
   void *poz = list + head;
   Node *current = (Node *)poz;
   uint8_t exitcount;
   exitcount = head == 0 ? 1 : 2;
   for (int i = 0; i < count; i++) {
      if (current->next == 0) {
         exitcount--;
      }
      entries[i].data = poz + 2 * sizeof(uint32_t);
      entries[i].size = current->size - 2 * sizeof(uint32_t);
      entries[i].mime_desc = getMimeDesc(magic, entries[i].data, entries[i].size);
      entries[i].mime = getMime(entries[i].mime_desc);
      poz = list + current->next;
      current = (Node *)poz;
   }
   return entries;
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

char *getMimeDesc(magic_t *magic, void *data, uint32_t size) {
   char *mime = strdup(magic_buffer(*magic, data, size));
   if (mime == NULL) {
      perror("Failed to get mime type");
      return NULL;
   }
   return mime;
}

mime_t getMime(const char *mime) {
   if (strstr(mime, "text")) {
      return MIME_TEXT;
   } else if (strstr(mime, "image/png")) {
      return MIME_IMAGE_PNG;
   } else if (strstr(mime, "image")) {
      return MIME_IMAGE_OTHER;
   } else if (strstr(mime, "audio")) {
      return MIME_AUDIO;
   } else if (strstr(mime, "video")) {
      return MIME_VIDEO;
   } else if (strstr(mime, "application")) {
      return MIME_APPLICATION;
   } else {
      return MIME_UNKNOWN;
   }
}
