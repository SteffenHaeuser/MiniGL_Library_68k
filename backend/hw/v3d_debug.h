/*
 * (C) 2025-2026 Dennis van der Boon
 */

void kprintf(STRPTR format, ...);

#ifdef DEBUG
#define D(x) do { kprintf x; } while (0)
#else
#define D(x) do { } while (0)
#endif

#define E(x) do { kprintf x; } while (0)
