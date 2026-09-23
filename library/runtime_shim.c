/* Minimal freestanding runtime for minigl.library.
 *
 * PiStorm3D's current libmgl.a was built as application code and therefore
 * references a small libc surface. Pulling a normal program C runtime into a
 * resident Amiga library also pulls startup/exit state, which is incorrect.
 * These implementations cover rendering-critical allocation and memory
 * operations. Console/file stdio is deliberately reduced; rebuild libmgl.a
 * without debug stdio later for a production release.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdarg.h>
#include <stddef.h>
#include "debug_output.h"

struct MiniGLAllocHeader
{
    ULONG total;
};

typedef void FILE;

#if 1
char *strncpy(char *dest, const char *src, size_t n)
{
    char *result = dest;

    while (n != 0 && *src != '\0')
    {
        *dest++ = *src++;
        --n;
    }

    while (n != 0)
    {
        *dest++ = '\0';
        --n;
    }

    return result;
}
#endif

void *malloc(size_t size)
{
    struct MiniGLAllocHeader *h;
    ULONG total;
    if (!size) size = 1;
    if (size > 0x7ffffff0UL) return 0;
    total = (ULONG)size + (ULONG)sizeof(*h);
    h = (struct MiniGLAllocHeader *)AllocMem(total, MEMF_PUBLIC);
    if (!h) return 0;
    h->total = total;
    return (void *)(h + 1);
}

void free(void *ptr)
{
    struct MiniGLAllocHeader *h;
    if (!ptr) return;
    h = ((struct MiniGLAllocHeader *)ptr) - 1;
    FreeMem(h, h->total);
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *p;
    if (count && size > ((size_t)-1) / count) return 0;
    total = count * size;
    p = malloc(total);
    if (p) {
        unsigned char *d = (unsigned char *)p;
        size_t i;
        for (i = 0; i < total; ++i) d[i] = 0;
    }
    return p;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) while (n--) *d++ = *s++;
    else if (d > s) { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

int puts(const char *s)
{
    if (s) dbg_puts(s);
    dbg_newline();
    return 0;
}

int putchar(int c)
{
    char b[2]; b[0] = (char)c; b[1] = 0; dbg_puts(b); return c;
}

int printf(const char *fmt, ...)
{
    /* Keep error paths visible without depending on a process stdio runtime. */
    if (fmt) dbg_puts(fmt);
    return 0;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    (void)stream;
    if (fmt) dbg_puts(fmt);
    return 0;
}

int vsprintf(char *dst, const char *fmt, va_list ap)
{
    (void)fmt; (void)ap;
    if (dst) *dst = 0;
    return 0;
}

FILE *fopen(const char *name, const char *mode)
{
    (void)name; (void)mode; return 0;
}

int fclose(FILE *stream)
{
    (void)stream; return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    (void)ptr; (void)size; (void)count; (void)stream; return 0;
}
