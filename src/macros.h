#pragma once
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
static const uint8_t log_level = 0;
#define LOG(imp, ...)                  \
   {                                   \
      if (log_level <= imp) {          \
         fprintf(stderr, __VA_ARGS__); \
      }                                \
   }
#define FTCHECK(x, e)                                                            \
   {                                                                             \
      uint32_t err = x;                                                          \
      if (err) {                                                                 \
         LOG(10, "Freetype error: %s %u:[%s]!\n", e, err, FT_Error_String(err)); \
         exit(1);                                                                \
      }                                                                          \
   }
#define ERRCHECK(x, e)                                               \
   {                                                                 \
      if (!(x)) {                                                    \
         LOG(10, "Error: %s %u:[%s]!\n", e, errno, strerror(errno)); \
         exit(1);                                                    \
      }                                                              \
   }
