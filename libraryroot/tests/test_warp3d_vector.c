/* Execute the real SDK macro against an artificial library vector table.
 * The +30 and -30 entries deliberately return different values. No Warp3D
 * installation or graphics hardware is needed to test the call direction.
 */
#include <stdio.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <utility/tagitem.h>
#include <Warp3D/Warp3D.h>
#include <inline/Warp3D.h>
extern unsigned char warp3d_probe_base[];
struct Library *Warp3DBase = (struct Library *)warp3d_probe_base;
static __attribute__((noinline)) W3D_Context *probe(void)
{
    ULONG error = 0;
    return W3D_CreateContext(&error, (struct TagItem *)0);
}
int main(void)
{
    ULONG result = (ULONG)probe();
    if (result == 0x12345678UL) {
        puts("PASS: W3D_CreateContext called base-30.");
        return 0;
    }
    if (result == 0x87654321UL)
        puts("FAIL: W3D_CreateContext called base+30 instead of base-30.");
    else
        printf("FAIL: unexpected vector result %08lx\n", result);
    return 1;
}
