#ifndef MINIGL_DEBUG_OUTPUT_H
#define MINIGL_DEBUG_OUTPUT_H

#include <exec/types.h>

void dbg_puts(const char *s);
void dbg_newline(void);
void dbg_u32(ULONG value);
void dbg_s32(LONG value);
void dbg_hex32(ULONG value);
void dbg_ptr(const void *ptr);
void dbg_float_bits(float value);

#endif
