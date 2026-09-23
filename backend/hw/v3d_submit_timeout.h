/*
 * (C) 2025-2026 Dennis van der Boon
 */

#ifndef V3D_SUBMIT_TIMEOUT_H
#define V3D_SUBMIT_TIMEOUT_H

#include "v3d_hw.h"
#include "../include/v3d_device.h"
#include "../include/v3d_frame.h"

/*
 * Bounded submit/wait functions for binning and render. The driver waits
 * for binning and render completion only through these, and both waits are
 * bounded: on real hardware the GPU's completion signal is, rarely, missed,
 * and an unbounded wait then busy-spins forever -- a hard hang that needs a
 * power cycle. A wait that runs out of iterations returns
 * v3d_wait_result_timed_out, which the callers in context.c
 * (gl_FramePresent, MGLFlushPendingRender) handle the same as
 * v3d_wait_result_error_detected: a dropped/glitched frame is far better
 * than a hard hang.
 */

/*
 * Poll bounds, in iterations. Each iteration does a real PiStorm register
 * read (v3d_get_binning_flush_count/v3d_get_render_frame_count), which has
 * real bus-bridge latency -- not a tight native-speed spin.
 *
 * Binning and render have independent bounds. A bound that is too low drops
 * good frames as false timeouts; one that is too high delays recovery from a
 * missed completion. Binning uses a flat bound, sized for 640x480: its cost
 * is more primitive/geometry-count-bound than pixel-count-bound.
 * Render cost (rasterization + tile store) scales with tile count
 * (width/64 * height/64), so a flat render bound sized for one resolution
 * can be too short at a higher one.
 * The render bound is therefore resolution-scaled: v3d_context_init, where
 * width/height become known, sets g_v3d_render_timeout_iterations to
 * (width*height)>>2, and v3d_wait_render_timeout reads that instead of a
 * macro directly, since it doesn't take a V3DContext* to compute it from.
 * V3D_TIMEOUT_ITERATIONS_RENDER is only that variable's value before
 * v3d_context_init runs.
 */
#ifndef V3D_TIMEOUT_ITERATIONS_BINNING
#define V3D_TIMEOUT_ITERATIONS_BINNING 100000UL
#endif

#ifndef V3D_TIMEOUT_ITERATIONS_RENDER
#define V3D_TIMEOUT_ITERATIONS_RENDER 300000UL
#endif

extern v3d_u32 g_v3d_render_timeout_iterations;

/*
 * Frame-pipelining split functions: v3d_submit_X just starts the command
 * list and returns the flush/frame-count snapshot to check later;
 * v3d_wait_X_timeout polls for it with a bounded iteration count (real
 * fresh register read every pass, no seed/copy -- see v3d_submit_timeout.c
 * for the full shape).
 */
v3d_u8 v3d_submit_binning(V3DDevice* device, V3DFrame* frame,
                          v3d_address binningCommandListStart,
                          v3d_address binningCommandListEnd,
                          v3d_address tileAllocation,
                          v3d_u32 tileAllocationSize,
                          v3d_address tileStateData);

v3d_wait_result v3d_wait_binning_timeout(V3DDevice* device, V3DFrame* frame, v3d_u8 lastFlush);

v3d_u8 v3d_submit_render(v3d_address renderCommandListStart, v3d_address renderCommandListEnd);

v3d_wait_result v3d_wait_render_timeout(v3d_u8 lastFrame);

#endif /* V3D_SUBMIT_TIMEOUT_H */
