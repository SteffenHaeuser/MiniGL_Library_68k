/*
 * (C) 2025-2026 Dennis van der Boon
 */

/*
 * Migrated from PoC/v3d_v3d.c (mailbox/init/mem-alloc layer). These
 * functions use none of RPIV3D's Library/W3D_Driver fields, only sysbase/
 * dosbase/BytesAllocated/deviceInfo -- so RPIV3D* becomes V3DDevice*
 * throughout (see v3d_device.h for why). SysBase and DOSBase are declared
 * as locals from device->sysbase/device->dosbase, keeping the exact
 * `SysBase`/`DOSBase` variable names the inline library-call stubs require.
 */

#include <stdarg.h>

#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <exec/alerts.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

#include "v3d_debug.h"
#include "devicetree_protos.h"
#include "v3d_regs.h"
#include "../include/v3d_device.h"

#define BUS_TO_PHYS(x) ((x)&~0xC0000000)

#define MAX_COMMAND_LENGTH 32

#define MBOX_READ   LE32(*((const volatile uint32_t*)(MailBox + 0x00)))
#define MBOX_STATUS LE32(*((const volatile uint32_t*)(MailBox + 0x18)))
#define MBOX_WRITE  *((volatile uint32_t*)(MailBox + 0x20))

#define MBOX_CHANNEL 8

#define MBOX_TX_FULL (1UL << 31)
#define MBOX_RX_EMPTY (1UL << 30)
#define MBOX_CHANMASK 0xF

static APTR DeviceTreeBase;
static ULONG MailBox;
static uint32_t MBReqStorage[MAX_COMMAND_LENGTH + 4];
static uint32_t* MBReq;

static unsigned wait_num;

int v3d_qpu_active(V3DDevice* device)
{
    if (!wait_num)
        return 1;
    if (((LE32(V3D_SRQCS)>>16) & 0xff) != wait_num)
        return 1;
    wait_num = 0; /* signal completion */
    return 0;
}

static void ZeroMem(void* d, ULONG sz)
{
    UBYTE* dst = d;
    while (sz--)
        *dst++ = 0;
}

static uint32_t mbox_recv(void)
{
    uint32_t rsp;
    do {
        while (MBOX_STATUS & MBOX_RX_EMPTY)
            NOP();

        NOP();
        rsp = MBOX_READ;
        NOP();
    } while ((rsp & MBOX_CHANMASK) != MBOX_CHANNEL);
    NOP();
    return rsp & ~MBOX_CHANMASK;
}

static void mbox_send(uint32_t* req)
{
    while (MBOX_STATUS & MBOX_TX_FULL)
        NOP();
    NOP();
    MBOX_WRITE = LE32(((uint32_t)req & ~MBOX_CHANMASK) | MBOX_CHANNEL);
}

static int mbox_transaction(V3DDevice* device, uint32_t* req)
{
    struct ExecBase* const SysBase = device->sysbase;
    ULONG len = LE32(*req) * 4;
    CachePreDMA(req, &len, 0);
    Forbid();
    mbox_send(req);
    mbox_recv();
    Permit();
    CachePostDMA(req, &len, 0);
    if (LE32(req[1]) == 0x80000000)
        return 0;
    D(("Mailbox transaction failed for command %08x: %08x\n", LE32(req[2]), LE32(req[1])));
    return LE32(req[1]);
}

static unsigned qpu_enable(V3DDevice* device, unsigned enable)
{
    int i=0;
    uint32_t* p = MBReq;

    p[i++] = 0; /* size */
    p[i++] = 0; /* process request */

    p[i++] = LE32(0x30012); /* (the tag id) */
    p[i++] = LE32(4); /* (size of the buffer) */
    p[i++] = LE32(4); /* (size of the data) */
    p[i++] = LE32(enable);

    p[i++] = 0; /* end tag */
    p[0] = LE32(i*sizeof *p); /* actual size */

    if (mbox_transaction(device, p))
        return -1;
    return LE32(p[5]);
}

static unsigned fwRev(V3DDevice* device)
{
    int i=0;
    uint32_t* p = MBReq;

    p[i++] = 0;
    p[i++] = 0;

    p[i++] = LE32(1);
    p[i++] = LE32(4);
    p[i++] = 0;
    p[i++] = 0;

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return -1;
    return LE32(p[5]);
}

static unsigned mbpowerV3D(V3DDevice* device, unsigned tagid, unsigned domain, unsigned state)
{
    int i=0;
    uint32_t* p = MBReq;

    p[i++] = 0;
    p[i++] = 0;

    p[i++] = LE32(tagid);
    p[i++] = LE32(8);
    p[i++] = 0;
    p[i++] = LE32(domain);
    p[i++] = LE32(state);

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return -1;
    return LE32(p[6]);
}

/*
 * AllocVec (backend/hw/v3d_mem_allocvec.c) is v3d_mem_alloc/v3d_mem_free's
 * DEFAULT implementation, in place of the RPi mailbox's GPU-memory
 * allocator. Motivation: the mailbox only has a limited amount of GPU
 * memory reserved, a real ceiling as MiniGLV3D's allocations grow;
 * AllocVec'd memory (visible to V3D via PiStorm's shared address space)
 * doesn't have that cap. AllocVec'd memory needs the ARM-side CPU cache
 * managed around V3D's accesses (CachePreDMA before V3D reads or writes
 * it, CachePostDMA before the CPU reads what V3D wrote) -- see
 * v3d_mem_allocvec.c.
 *
 * Defining V3D_MEM_USE_MAILBOX compiles this file's own mailbox-backed
 * v3d_mem_alloc/v3d_mem_free (and their mailbox-only helpers below), for
 * a build that links them in place of v3d_mem_allocvec.c's -- kept
 * available as a fallback, not the default. Every other function in this
 * file (device init, register discovery, QPU enable, etc.) is unaffected
 * either way.
 */
#ifdef V3D_MEM_USE_MAILBOX

static unsigned mem_alloc(V3DDevice* device, unsigned size, unsigned align, unsigned flags)
{
    int i=0;
    uint32_t* p = MBReq;
    p[i++] = 0;
    p[i++] = 0;

    p[i++] = LE32(0x3000c);
    p[i++] = LE32(12);
    p[i++] = LE32(12);
    p[i++] = LE32(size);
    p[i++] = LE32(align);
    p[i++] = LE32(flags);

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return 0;
    return LE32(p[5]);
}

static unsigned mem_free(V3DDevice* device, unsigned handle)
{
    int i=0;
    uint32_t* p = MBReq;
    p[i++] = 0;
    p[i++] = 0;

    p[i++] = LE32(0x3000f);
    p[i++] = LE32(4);
    p[i++] = LE32(4);
    p[i++] = LE32(handle);

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return -1;
    return LE32(p[5]);
}

static unsigned mem_lock(V3DDevice* device, unsigned handle)
{
    int i=0;
    uint32_t* p = MBReq;
    p[i++] = 0;
    p[i++] = 0;
    p[i++] = LE32(0x3000d);
    p[i++] = LE32(4);
    p[i++] = LE32(4);
    p[i++] = LE32(handle);

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return 0;
    return LE32(p[5]);
}

static unsigned mem_unlock(V3DDevice* device, unsigned handle)
{
    int i=0;
    uint32_t* p = MBReq;
    p[i++] = 0;
    p[i++] = 0;

    p[i++] = LE32(0x3000e);
    p[i++] = LE32(4);
    p[i++] = LE32(4);
    p[i++] = LE32(handle);

    p[i++] = 0;
    p[0] = LE32(i*sizeof *p);

    if (mbox_transaction(device, p))
        return -1;
    return LE32(p[5]);
}

#endif /* V3D_MEM_USE_MAILBOX */

void power_on_V3D(V3DDevice* device)
{
    struct DOSBase* const DOSBase = device->dosbase;

    PM_GRAFX = ((PM_GRAFX) | PASSWORD | PM_V3DRSTN);
    Delay(1);

    ASB_V3D_S_CTRL = (((ASB_V3D_S_CTRL) | PASSWORD) & ~ASB_REQ_STOP);
    while ((ASB_V3D_S_CTRL) & ASB_ACK);

    ASB_V3D_M_CTRL = (((ASB_V3D_M_CTRL) | PASSWORD) & ~ASB_REQ_STOP);
    while ((ASB_V3D_M_CTRL) & ASB_ACK);
}

void v3d_read_info(V3DDevice* device)
{
    ULONG identity1 = LE32(V3D_HUB_IDENT1);
    ULONG version = (((identity1 >> V3D_HUB_IDENT1_TVER_SHIFT) & V3D_HUB_IDENT1_TVER_MASK) * 10) +
                  ((identity1 >> V3D_HUB_IDENT1_REV_SHIFT) & V3D_HUB_IDENT1_REV_MASK);
    device->deviceInfo.ver = version;
    device->deviceInfo.has_accumulators = version < 71;
}

/*
    Some properties, like e.g. #size-cells, are not always available in a key, but in that case the properties
    should be searched for in the parent. The process repeats recursively until either root key is found
    or the property is found, whichever occurs first
*/
static CONST_APTR GetPropValueRecursive(APTR key, CONST_STRPTR property)
{
    do {
        APTR prop = DT_FindProperty(key, property);

        if (prop)
        {
            return DT_GetPropValue(prop);
        }

        key = DT_GetParent(key);
    } while (key);

    return NULL;
}

#ifdef V3D_MEM_USE_MAILBOX

void v3d_mem_free(V3DDevice* device, v3d_mem* m)
{
    if (m->handle) {
        device->BytesAllocated -= m->size;

        D(("Freeing %lu bytes, busaddr = %lx phys = %lx\n", m->size, m->busaddr, BUS_TO_PHYS(m->busaddr)));

        if (m->busaddr)
            mem_unlock(device, m->handle);
        mem_free(device, m->handle);
    }
    ZeroMem(m, sizeof(*m));
}

int v3d_mem_alloc(V3DDevice* device, v3d_mem* m, unsigned size)
{
    const uint32_t align = 16;

    ZeroMem(m, sizeof(*m));
    m->size = (size + align - 1) & ~(align - 1);
    m->handle = mem_alloc(device, m->size, 8, MEM_FLAG_DIRECT);
    if (!m->handle) {
        D(("Failed to alloc mem\n"));
        return -1;
    }
    m->busaddr = mem_lock(device, m->handle);
    if (!m->busaddr) {
        D(("Failed to lock memory\n"));
        v3d_mem_free(device, m);
        return -2;
    }
    m->hostptr = (void*)BUS_TO_PHYS(m->busaddr);
    if (!m->hostptr) {
        D(("Failed to map memory\n"));
        v3d_mem_free(device, m);
        return -3;
    }

    D(("Allocated %lu bytes, busaddr = %lx phys = %lx\n", size, m->busaddr, BUS_TO_PHYS(m->busaddr)));

    device->BytesAllocated += m->size;
    return 0;
}

#endif /* V3D_MEM_USE_MAILBOX */

void v3d_free(V3DDevice* device)
{
    int ret;

    D(("v3d_free\n"));
    /* Leak check: v3d_mem_alloc adds ->size to device->BytesAllocated and
     * v3d_mem_free subtracts ->size; code that shrinks ->size in between
     * (align_mem_4096, alloc_spill_block) subtracts the difference itself. */
    if (device->BytesAllocated) {
        D(("Still %lu bytes allocated!\n", (ULONG)device->BytesAllocated));
    }
    ret = qpu_enable(device, 0);
    D(("qpu_enable(0) %ld\n", ret));
}

int v3d_init(V3DDevice* device)
{
    int ret = -1;
    struct ExecBase* const SysBase = device->sysbase;

    D(("v3d_init: enter\n"));

    MBReq = (uint32_t*)(((uintptr_t)MBReqStorage + 15) & ~15);
    if ((DeviceTreeBase = OpenResource("devicetree.resource")) != NULL) {
        APTR key = DT_OpenKey("/aliases");
        if (key) {
            const char* mbox_alias = DT_GetPropValue(DT_FindProperty(key, "mailbox"));
            DT_CloseKey(key);
            if (mbox_alias) {
                key = DT_OpenKey(mbox_alias);
                if (key) {
                    ULONG address_cells = 1;
                    CONST ULONG *adr = GetPropValueRecursive(key, "#address-cells");
                    CONST ULONG *reg = DT_GetPropValue(DT_FindProperty(key, "reg"));

                    if (adr != NULL) address_cells = *adr;
                    MailBox = reg[(1 * address_cells) - 1];
                    DT_CloseKey(key);
                    D(("v3d_init: mailbox alias resolved, MailBox=0x%lx\n", MailBox));

                    /* Open /soc key and learn about VC4 to CPU mapping. Use it to adjust the addresses obtained above */
                    key = DT_OpenKey("/soc");
                    if (key) {
                        ULONG cpu_address_cells = 1;
                        const ULONG * addr;
                        const ULONG * cpu_addr;
                        const ULONG *reg2;
                        ULONG phys_v3d;
                        ULONG phys_cpu;

                        address_cells = 1;

                        addr = GetPropValueRecursive(key, "#address-cells");
                        cpu_addr = DT_GetPropValue(DT_FindProperty(DT_OpenKey("/"), "#address-cells"));

                        if (addr != NULL) address_cells = *addr;
                        if (cpu_addr != NULL) cpu_address_cells = *cpu_addr;

                        reg2 = DT_GetPropValue(DT_FindProperty(key, "ranges"));

                        phys_v3d = reg2[address_cells - 1];
                        phys_cpu = reg2[address_cells + cpu_address_cells - 1];

                        MailBox = ((ULONG)MailBox - phys_v3d + phys_cpu);

                        DT_CloseKey(key);
                        D(("v3d_init: /soc mapped, Mailbox = 0x%lx\n", MailBox));

                        ret = fwRev(device);
                        D(("v3d_init: firmware check result: 0x%08lx\n", ret));

                        /* VC6 specific v3d power enable */
                        power_on_V3D(device);
                        D(("v3d_init: power_on_V3D done\n"));

                        v3d_read_info(device);

                        D(("v3d_init: V3D ident: %08lx %08lx %08lx\n", LE32(V3D_IDENT0), LE32(V3D_IDENT1), LE32(V3D_IDENT2)));
                        D(("v3d_init: device revision: %ld\n", device->deviceInfo.ver));

                        if (LE32(V3D_IDENT0) != 0x04443356)
                        {
                            E(("v3d_init: V3D core NOT started properly, IDENT0=%08lx (expected 04443356) -- returning -6\n", LE32(V3D_IDENT0)));
                            return -6;
                        } else {
                            D(("v3d_init: V3D core started properly\n"));
                        }

                        D(("v3d_init: L2 Cache state: %08lx\n", LE32(V3D_L2CACTL)));

                        /* Enable performance counters */
                        {
                            ULONG pc_i;
                            for (pc_i = 0; pc_i <= 29-16; ++pc_i) {
                                V3D_PCTRS(pc_i) = LE32(16 + pc_i);
                            }
                        }
                        V3D_PCTRE = V3D_PCTRC = LE32((1U<<(30-16)) - 1); /* Clear and enable */
                        V3D_PCTRE = LE32(LE32(V3D_PCTRE) | 0x80000000); /* Undocumented: MSB means enable */

                        ret = 0;
                    } else {
                        E(("v3d_init: Could not open /soc -- returning -1\n"));
                        MailBox = 0;
                        ret = -1;
                    }
                } else {
                    E(("v3d_init: Could not open mail box alias -- returning -2\n"));
                    ret = -2;
                }
            } else {
                E(("v3d_init: Not mailbox alias -- returning -3\n"));
                ret = -3;
            }
        } else {
            E(("v3d_init: Could not open aliases -- returning -4\n"));
            ret = -4;
        }
    } else {
        E(("v3d_init: Could not open devicetree.resource -- returning -5\n"));
        ret = -5;
    }
    D(("v3d_init: exit, ret=%ld\n", (LONG)ret));
    return ret;
}
