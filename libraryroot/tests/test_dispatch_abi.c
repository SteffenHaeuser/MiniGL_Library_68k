#include <stddef.h>
#include <stdio.h>
#define MINIGL_LIBRARY_BUILD 1
#include <libraries/minigl_dispatch.h>
/* V23 published suffix is frozen. V25 adds fifteen pointers after it. */
#define CHECK(name, expr) typedef char name[(expr) ? 1 : -1]
CHECK(abi3, MINIGL_DISPATCH_ABI_VERSION == 3);
CHECK(v23_prefix, offsetof(MGLDispatchTable, GLClientActiveTextureARB) == 536);
CHECK(v23_interleaved, offsetof(MGLDispatchTable, GLInterleavedArrays) == 540);
CHECK(v23_multidraw, offsetof(MGLDispatchTable, GLMultiDrawArrays) == 544);
CHECK(v23_suffix, offsetof(MGLDispatchTable, mglChooseZBufferDepth) == 560);
CHECK(v25_append, offsetof(MGLDispatchTable, GLLineWidth) == 564);
CHECK(v25_tail, offsetof(MGLDispatchTable, GLReadBuffer) == 620);
CHECK(v27_window, offsetof(MGLDispatchTable, MGLCreateContextFromWindow) == 624);
CHECK(v27_bitmap, offsetof(MGLDispatchTable, MGLCreateContextFromBitMap) == 628);
CHECK(v27_size, sizeof(MGLDispatchTable) == 632);
CHECK(classic_array_enum, MGL_ARRAY_TRANSFORMATIONS == 242);
CHECK(classic_lock_enum, MGL_LOCK_AUTOMATIC == 228);
int main(void)
{
    puts("PASS: V27 ABI 3 table = 632 bytes, V23 offsets preserved, Classic enums.");
    return 0;
}
