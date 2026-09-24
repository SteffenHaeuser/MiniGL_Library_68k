#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/dos.h>
#include "debug_output.h"

extern struct DosLibrary *DOSBase;

static ULONG text_length(const char *s)
{
    ULONG n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

static void write_text(const char *s, ULONG n)
{
    BPTR out;
    if (!DOSBase || !s || !n) return;
    out = Output();
    if (out) Write(out, (APTR)s, (LONG)n);
}

void dbg_puts(const char *s)
{
    write_text(s, text_length(s));
}

void dbg_newline(void)
{
    static const char nl = '\n';
    write_text(&nl, 1);
}

void dbg_u32(ULONG value)
{
    char buf[10];
    ULONG pos = sizeof(buf);
    do {
        buf[--pos] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && pos);
    write_text(buf + pos, sizeof(buf) - pos);
}

void dbg_s32(LONG value)
{
    ULONG magnitude;
    if (value < 0) {
        dbg_puts("-");
        magnitude = (ULONG)(-(value + 1)) + 1;
    } else {
        magnitude = (ULONG)value;
    }
    dbg_u32(magnitude);
}

void dbg_hex32(ULONG value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; ++i)
        buf[2 + i] = hex[(value >> (28 - i * 4)) & 15];
    write_text(buf, sizeof(buf));
}

void dbg_ptr(const void *ptr)
{
    dbg_hex32((ULONG)ptr);
}

void dbg_float_bits(float value)
{
    union { float f; ULONG u; } bits;
    bits.f = value;
    dbg_hex32(bits.u);
}
