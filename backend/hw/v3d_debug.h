/*
 * (C) 2025-2026 Dennis van der Boon
 */

/* const char*, not STRPTR: every caller passes a string literal, and STRPTR is
 * unsigned char* -- which made each of those a -Wpointer-sign warning. The
 * RawDoFmt call inside casts once instead. */
void kprintf(const char* format, ...);

#ifdef DEBUG
#define D(x) do { kprintf x; } while (0)
#else
#define D(x) do { } while (0)
#endif

#define E(x) do { kprintf x; } while (0)
