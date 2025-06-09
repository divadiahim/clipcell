#pragma once
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t log_level = 10;

#define LOG(imp, ...)                     \
    {                                     \
        if (log_level <= imp) {           \
            fprintf(stderr, __VA_ARGS__); \
        }                                 \
    }
#define FTCHECK(x, e)                                                               \
    {                                                                               \
        uint32_t err = x;                                                           \
        if (err) {                                                                  \
            LOG(10, "Freetype error: %s %u:[%s]!\n", e, err, FT_Error_String(err)); \
            exit(1);                                                                \
        }                                                                           \
    }
#define ERRCHECK(x, e)                                                  \
    {                                                                   \
        if (!(x)) {                                                     \
            LOG(10, "Error: %s %u:[%s]!\n", e, errno, strerror(errno)); \
            exit(1);                                                    \
        }                                                               \
    }

#define ASSERT_EXIT(x, e) \
    {                     \
        if (!(x)) {       \
            LOG(10, e);   \
            exit(1);      \
        }                 \
    }

#define ASSERT_CONTINUE(x, e) \
    {                         \
        if (!(x)) {           \
            LOG(10, e);       \
            continue;         \
        }                     \
    }

#define errExit(msg)                            \
    do {                                        \
        perror(msg);                            \
        fprintf(stderr, "Exiting: %s\n", msg);  \
        exit(EXIT_FAILURE);                     \
    } while (0)
