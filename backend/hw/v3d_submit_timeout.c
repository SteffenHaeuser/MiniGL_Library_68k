/*
 * (C) 2025-2026 Dennis van der Boon
 */

#include "v3d_submit_timeout.h"
#include "v3d_regs.h"
#include "v3d_debug.h"

/* Resolution-scaled render timeout -- see v3d_submit_timeout.h's own
 * comment. Holds V3D_TIMEOUT_ITERATIONS_RENDER until v3d_context_init
 * (backend/hw/v3d_context.c) sets the (width*height)>>2 bound for the
 * actual configured resolution. */
v3d_u32 g_v3d_render_timeout_iterations = V3D_TIMEOUT_ITERATIONS_RENDER;

/*
 * Frame-pipelining split functions: v3d_submit_X just starts the command
 * list and returns the flush/frame-count snapshot to check later;
 * v3d_wait_X_timeout polls for it with a bounded iteration count (see
 * v3d_submit_timeout.h).
 */
v3d_u8 v3d_submit_binning(V3DDevice* device, V3DFrame* frame,
                          v3d_address binningCommandListStart,
                          v3d_address binningCommandListEnd,
                          v3d_address tileAllocation,
                          v3d_u32 tileAllocationSize,
                          v3d_address tileStateData)
{
    /* Read BEFORE submit. */
    v3d_u8 lastFlush = v3d_get_binning_flush_count();
    /* The spill cap is per binning JOB -- see v3d_frame.h. */
    v3d_frame_binning_job_begin(frame);
    v3d_start_binning_commands(binningCommandListStart, binningCommandListEnd,
                                tileAllocation, tileAllocationSize, tileStateData);
    return lastFlush;
}

/* lastFlush is the snapshot returned by v3d_submit_binning. */
v3d_wait_result v3d_wait_binning_timeout(V3DDevice* device, V3DFrame* frame, v3d_u8 lastFlush)
{
    v3d_u8 currentFlushCount;
    v3d_u32 iterations = 0;

    D(("v3d_wait_binning_timeout: starting wait, lastFlush=%ld\n", (LONG)lastFlush));

    /* Read-first, no-seed shape: the count is read at the top of every
     * pass, never before the loop. */
    for (;;)
    {
        ULONG intsts;      /* byte-swapped, for testing V3D_INT_* constants */
        ULONG intsts_raw;  /* as read; what INT_CLR must be written back */
        ULONG status;

        currentFlushCount = v3d_get_binning_flush_count();

        if (currentFlushCount > lastFlush || (currentFlushCount == 0 && lastFlush == 255))
            break;

        /* The register read is RAW (the GPU's little-endian word as the CPU
         * sees it), but V3D_INT_* are declared in NATURAL bit order, so the
         * test must use the swapped word: raw
         * 0x03000000 is logical 0x3, i.e. logical bits 0-1 land in raw bits
         * 24-25, so logical bit 2 (OUTOMEM) is raw 0x04000000 and never 0x04.
         * Testing the raw word would test logical bit 26 instead, so a real
         * binner out-of-memory would never reach the handler.
         *
         * Two conventions coexist in v3d_regs.h: V3D_CTNCS_CTERR (0x08000000)
         * is PRE-SWAPPED for a raw compare, while V3D_INT_* are natural-order
         * for a swapped compare.
         *
         * INT_CLR still gets the RAW word: it is write-1-to-clear against the
         * hardware's own bit positions. */
        intsts_raw = V3D_CTL_INT_STS;
        intsts = (v3d_u32)LE32(intsts_raw);
        if (intsts & V3D_INT_OUTOMEM)
        {
            v3d_mem* spill;

            V3D_CTL_INT_CLR = intsts_raw;

            spill = v3d_frame_add_spill_block(device, frame);
            if (!spill)
            {
                D(("v3d_wait_binning_timeout: spill block alloc failed\n"));
                return v3d_wait_result_error_detected;
            }

            V3D_BPOA = LE32(spill->busaddr);
            /* The fixed block size, not spill->size: a spill block is
             * allocated with 4096 bytes of slack and then 4096-aligned, so
             * ->size is 266240 minus the alignment offset -- not a 4 KB
             * multiple. MESA's kernel always hands the binner whole pages;
             * V3D_SPILL_BLOCK_SIZE (262144) always fits. */
            V3D_BPOS = LE32(V3D_SPILL_BLOCK_SIZE);
            continue;
        }

        status = V3D_CT0CS;
        if (status & V3D_CTNCS_CTERR)
        {
            D(("v3d_wait_binning_timeout: CTERR detected\n"));
            return v3d_wait_result_error_detected;
        }

        if (++iterations > V3D_TIMEOUT_ITERATIONS_BINNING)
        {
            D(("v3d_wait_binning_timeout: TIMED OUT after %lu iterations, currentFlushCount stuck at %ld (lastFlush was %ld)\n",
               (ULONG)iterations, (LONG)currentFlushCount, (LONG)lastFlush));
            return v3d_wait_result_timed_out;
        }
    }
    /* MESA-exact: the kernel acknowledges INT_STS in its IRQ handler after
     * every job (write-1-to-clear, raw word back). This driver polls BFC/RFC
     * instead, so it acknowledges here; without this, FLDONE/FRDONE would stay
     * set from earlier jobs. */
    V3D_CTL_INT_CLR = V3D_CTL_INT_STS;
    return v3d_wait_result_success;
}

v3d_u8 v3d_submit_render(v3d_address renderCommandListStart, v3d_address renderCommandListEnd)
{
    /* Read BEFORE submit. */
    v3d_u8 lastFrame = v3d_get_render_frame_count();
    v3d_start_render_commands(renderCommandListStart, renderCommandListEnd);
    return lastFrame;
}

/* Frame-pipelining instrumentation: how many poll iterations
 * the LAST call to v3d_wait_render_timeout actually needed -- 0 means the
 * GPU's render was already done the instant we checked (nothing to wait
 * for), a large number means we
 * genuinely blocked waiting on the GPU. Each iteration is a real
 * PiStorm-bridge register read (v3d_get_render_frame_count), not a
 * native-speed spin, so iteration count is a real (if approximate) proxy
 * for elapsed wait time without needing a separate timer call. */
v3d_u32 g_mglv3d_render_wait_iterations = 0;

v3d_wait_result v3d_wait_render_timeout(v3d_u8 lastFrame)
{
    v3d_u8 currentFrameCount;
    v3d_u32 status;
    v3d_u32 iterations = 0;

    D(("v3d_wait_render_timeout: starting wait, lastFrame=%ld\n", (LONG)lastFrame));

    /* Read-first, no-seed shape: the count is read at the top of every
     * pass, never before the loop. */
    for (;;)
    {
        currentFrameCount = v3d_get_render_frame_count();

        if (currentFrameCount > lastFrame || (currentFrameCount == 0 && lastFrame == 255))
            break;

        status = V3D_CT1CS;
        if (status & V3D_CTNCS_CTERR)
        {
            g_mglv3d_render_wait_iterations = iterations;
            D(("v3d_wait_render_timeout: CTERR detected\n"));
            return v3d_wait_result_error_detected;
        }

        if (++iterations > g_v3d_render_timeout_iterations)
        {
            g_mglv3d_render_wait_iterations = iterations;

            /* Where thread 1 stopped, sampled twice with real bridge reads in
             * between: unchanged = wedged at that address, advanced = the
             * render is still progressing and the timeout is too short. Raw
             * register words (byte-swapped by the reader, as everywhere here). */
            {
                v3d_u32 ca_a = V3D_CT1CA;
                v3d_u32 ca_b;
                (void)v3d_get_render_frame_count();
                (void)V3D_CT1CS;
                (void)v3d_get_render_frame_count();
                ca_b = V3D_CT1CA;
                E(("v3d_wait_render_timeout: TIMED OUT after %lu iterations, currentFrameCount stuck at %ld (lastFrame was %ld)\n",
                   (ULONG)iterations, (LONG)currentFrameCount, (LONG)lastFrame));
                E(("v3d_wait_render_timeout: CT1CA=%lx then %lx (%s), CT1EA=%lx, CT1CS=%lx\n",
                   (ULONG)ca_a, (ULONG)ca_b, (ca_a == ca_b) ? "WEDGED" : "still advancing",
                   (ULONG)V3D_CT1EA, (ULONG)V3D_CT1CS));
            }
            return v3d_wait_result_timed_out;
        }
    }
    g_mglv3d_render_wait_iterations = iterations;
    V3D_CTL_INT_CLR = V3D_CTL_INT_STS; /* see v3d_wait_binning_timeout */
    return v3d_wait_result_success;
}
