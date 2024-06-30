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

uint16_t get_enr(void *data) {
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

uint32_t removeNode(void *data, uint32_t index) {
   void *list = data + sizeof(uint32_t);
   uint32_t *head = (uint32_t *)data;
   Node *curr_poz = list + index;
   uint32_t temp_size = curr_poz->size;
   void *poz = list + *head;
   uint32_t temp_final_size = ((Node *)poz)->size;
   Node *current = (Node *)poz;
   uint8_t exitcount;
   exitcount = head == 0 ? 1 : 2;
   do {
      uint32_t temp_next = current->next;
      if (current->next == 0) {
         exitcount--;
      }
      if (current->next != index && index != *head) {
         current->next -= temp_size;
      } else if (index != *head) {
         current->next = curr_poz->next;
         for (int i = 0; i < *head - index + temp_final_size; i++) {
            ((char *)curr_poz)[i] = ((char *)curr_poz)[i + temp_size];
         }
         *head -= temp_size;
         break;
      } else {
         *head = curr_poz->next;
         break;
      }
      poz = list + temp_next;
      current = (Node *)poz;
   } while (exitcount);
   return temp_size;
}

Entry *get_entries(void *data, magic_t *magic, int count) {
   if (count == 0) {
      return NULL;
   }
   void *list = data + sizeof(uint32_t);
   uint32_t head = *(uint32_t *)data;
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
      entries[i].mime_desc = getMimeDesc(magic, entries[i].data, entries[i].size);  // saving also the mime char for debugging unknown mime types
      entries[i].mime = getMime(entries[i].mime_desc);
      entries[i].poz = poz - list;
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

char *getMimeDesc(magic_t *magic, void *data, uint32_t size) {
   char *mime = strdup(magic_buffer(*magic, data, size));
   if (mime == NULL) {
      perror("Failed to get mime type");
      return NULL;
   }
   return mime;
}

mime_t getMime(const char *mime) {
   if (strstr(mime, "text") || strstr(mime, "octet-stream")) {
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
