/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_DEVICE_H
#define V3D_DEVICE_H

#include "v3d_types.h"

/*
 * V3DDevice -- derived from RPIV3D (PoC/v3d_structs.h:49-64). It keeps
 * only the RPIV3D fields v3d_init/v3d_mem_alloc/v3d_mem_free/v3d_free/
 * v3d_qpu_active use (sysbase, dosbase, BytesAllocated, deviceInfo). The
 * rest is not carried over; that includes the Warp3D driver registration
 * (struct Library lib, W3D_Driver driver/owndriver/end), which MiniGLV3D
 * does not need since it links this in directly rather than registering
 * as a loadable Warp3D driver.
 */

struct v3d_device_info {
    unsigned char ver;              /* simple V3D version: major*10 + minor */
    unsigned char rev;
    int vpm_size;                    /* VPM size, in bytes */
    int qpu_count;                   /* NSLC * QUPS from the core's IDENT registers */
    char has_accumulators;           /* whether the hw has accumulator registers */
};

typedef struct V3DDevice {
    struct ExecBase* sysbase;
    struct DOSBase*  dosbase;
    v3d_u32 BytesAllocated;
    struct v3d_device_info deviceInfo;
} V3DDevice;

int  v3d_init(V3DDevice* device);
void v3d_free(V3DDevice* device);
int  v3d_mem_alloc(V3DDevice* device, v3d_mem* m, unsigned size);
void v3d_mem_free(V3DDevice* device, v3d_mem* m);
int  v3d_qpu_active(V3DDevice* device);

#endif /* V3D_DEVICE_H */
