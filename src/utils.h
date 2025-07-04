/*
 * Copyright © 2012 Ran Benita <ran234@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "config.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#else
/* Required on Windows where unistd.h doesn't exist */
# define R_OK    4               /* Test for read permission.  */
# define W_OK    2               /* Test for write permission.  */
# define X_OK    1               /* Test for execute permission.  */
# define F_OK    0               /* Test for existence.  */
#endif

#ifdef _WIN32
# include <direct.h>
# include <io.h>
# include <BaseTsd.h>
# ifndef S_ISDIR
#  define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
# endif
# ifndef S_ISREG
#  define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
# endif
typedef SSIZE_T ssize_t;
#endif

#include "darray.h"

#define ARRAY_SIZE(arr) ((sizeof(arr) / sizeof(*(arr))))

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Round up @a so it's divisible by @b. */
#define ROUNDUP(a, b) (((a) + (b) - 1) / (b) * (b))

#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
#define CONCAT(x,y) x ## y
#define CONCAT2(x,y) CONCAT(x,y)

/* Check if a Unicode code point is valid in a string literal */
static inline bool
is_valid_char(uint32_t cp)
{
    /* Currently we only check for NULL character, but this could be extended
     * in the future to further ASCII control characters. */
    return cp != 0;
}

/* Check if a Unicode code point is a surrogate.
 * Those code points are used only in UTF-16 encodings. */
static inline bool
is_surrogate(uint32_t cp)
{
    return (cp >= 0xd800 && cp <= 0xdfff);
}

char
to_lower(char c);

int
istrcmp(const char *a, const char *b);

int
istrncmp(const char *a, const char *b, size_t n);

static inline bool
streq(const char *s1, const char *s2)
{
    assert(s1 && s2);
    return strcmp(s1, s2) == 0;
}

static inline bool
streq_null(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL)
        return s1 == s2;
    return streq(s1, s2);
}

static inline bool
streq_not_null(const char *s1, const char *s2)
{
    if (!s1 || !s2)
        return false;
    return streq(s1, s2);
}

static inline bool
istreq(const char *s1, const char *s2)
{
    return istrcmp(s1, s2) == 0;
}

static inline bool
istrneq(const char *s1, const char *s2, size_t len)
{
    return istrncmp(s1, s2, len) == 0;
}

#define istreq_prefix(s1, s2) istrneq(s1, s2, sizeof(s1) - 1)

static inline char *
strdup_safe(const char *s)
{
    return s ? strdup(s) : NULL;
}

static inline size_t
strlen_safe(const char *s)
{
    return s ? strlen(s) : 0;
}

static inline bool
isempty(const char *s)
{
    return s == NULL || s[0] == '\0';
}

static inline const char *
strnull(const char *s)
{
    return s ? s : "(null)";
}

static inline const char *
strempty(const char *s)
{
    return s ? s : "";
}

static inline void *
memdup(const void *mem, size_t nmemb, size_t size)
{
    void *p = calloc(nmemb, size);
    if (p)
        memcpy(p, mem, nmemb * size);
    return p;
}

#if !(defined(HAVE_STRNDUP) && HAVE_STRNDUP)
static inline char *
strndup(const char *s, size_t n)
{
    size_t slen = strlen(s);
    size_t len = MIN(slen, n);
    char *p = malloc(len + 1);
    if (!p)
        return NULL;
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}
#endif

/* ctype.h is locale-dependent and has other oddities. */
static inline bool
is_ascii(char ch)
{
    return (ch & ~0x7f) == 0;
}

static inline bool
is_space(char ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

static inline bool
is_alpha(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static inline bool
is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static inline bool
is_alnum(char ch)
{
    return is_alpha(ch) || is_digit(ch);
}

static inline bool
is_xdigit(char ch)
{
    return
        (ch >= '0' && ch <= '9') ||
        (ch >= 'a' && ch <= 'f') ||
        (ch >= 'A' && ch <= 'F');
}

static inline bool
is_graph(char ch)
{
    /* See table in ascii(7). */
    return ch >= '!' && ch <= '~';
}

/*
 * Return the bit position of the most significant bit.
 * Note: this is 1-based! It's more useful this way, and returns 0 when
 * mask is all 0s.
 */
static inline unsigned int
msb_pos(uint32_t mask)
{
    unsigned int pos = 0;
    while (mask) {
        pos++;
        mask >>= 1u;
    }
    return pos;
}

static inline int
one_bit_set(uint32_t x)
{
    return x && (x & (x - 1)) == 0;
}

bool
map_file(FILE *file, char **string_out, size_t *size_out);

void
unmap_file(char *string, size_t size);

static inline bool
check_eaccess(const char *path, int mode)
{
#if defined(HAVE_EACCESS)
    if (eaccess(path, mode) != 0)
        return false;
#elif defined(HAVE_EUIDACCESS)
    if (euidaccess(path, mode) != 0)
        return false;
#endif

    return true;
}

FILE*
open_file(const char *path);

#if defined(HAVE_SECURE_GETENV)
# define secure_getenv secure_getenv
#elif defined(HAVE___SECURE_GETENV)
# define secure_getenv __secure_getenv
#else
# define secure_getenv getenv
#endif

#if defined(HAVE___BUILTIN_EXPECT)
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

/* Compiler Attributes */

/* Private functions only exposed in tests. */
#ifdef ENABLE_PRIVATE_APIS
# if defined(__GNUC__) && !defined(__CYGWIN__)
#  define XKB_EXPORT_PRIVATE __attribute__((visibility("default")))
# elif defined(_WIN32)
#  define XKB_EXPORT_PRIVATE __declspec(dllexport)
# else
#  define XKB_EXPORT_PRIVATE
# endif
#else
# define XKB_EXPORT_PRIVATE
#endif

#if defined(__MINGW32__)
# define ATTR_PRINTF(x,y) __attribute__((__format__(__MINGW_PRINTF_FORMAT, x, y)))
#elif defined(__GNUC__)
# define ATTR_PRINTF(x,y) __attribute__((__format__(__printf__, x, y)))
#else
# define ATTR_PRINTF(x,y)
#endif

#if defined(__GNUC__)
# define ATTR_NORETURN __attribute__((__noreturn__))
#else
# define ATTR_NORETURN
#endif /* GNUC  */

#if defined(__GNUC__)
#define ATTR_MALLOC  __attribute__((__malloc__))
#else
#define ATTR_MALLOC
#endif

#if defined(__GNUC__)
# define ATTR_NULL_SENTINEL __attribute__((__sentinel__))
#else
# define ATTR_NULL_SENTINEL
#endif

#if defined(__GNUC__)
#define ATTR_PACKED  __attribute__((__packed__))
#else
#define ATTR_PACKED
#endif

#if !(defined(HAVE_ASPRINTF) && HAVE_ASPRINTF)
XKB_EXPORT_PRIVATE int asprintf(char **strp, const char *fmt, ...) ATTR_PRINTF(2, 3);
# if !(defined(HAVE_VASPRINTF) && HAVE_VASPRINTF)
#  include <stdarg.h>
XKB_EXPORT_PRIVATE int vasprintf(char **strp, const char *fmt, va_list ap);
# endif /* !HAVE_VASPRINTF */
#endif /* !HAVE_ASPRINTF */

static inline bool
ATTR_PRINTF(3, 4)
snprintf_safe(char *buf, size_t sz, const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = vsnprintf(buf, sz, format, ap);
    va_end(ap);

    return rc >= 0 && (size_t)rc < sz;
}

static inline char *
ATTR_PRINTF(1, 0)
vasprintf_safe(const char *fmt, va_list args)
{
    char *str;
    int len;

    len = vasprintf(&str, fmt, args);

    if (len == -1)
        return NULL;

    return str;
}

/**
 * A version of asprintf that returns the allocated string or NULL on error.
 */
static inline char *
ATTR_PRINTF(1, 2)
asprintf_safe(const char *fmt, ...)
{
    va_list args;
    char *str;

    va_start(args, fmt);
    str = vasprintf_safe(fmt, args);
    va_end(args);

    return str;
}
