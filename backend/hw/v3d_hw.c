/*
 * Implementation half of PoC/v3d_v3d.h's single-header
 * (#define V3D_IMPLEMENTATION) design; see v3d_hw.h for why this is split
 * into a normal .h/.c pair. License/provenance for this code is in the
 * comment header at the top of v3d_hw.h (MIT-style, Macoy Madson, with
 * portions derived from Mesa per that file's own attribution).
 *
 * v3d_power_on() and v3d_reset(), declared in v3d_hw.h, have no body here;
 * the power-on sequence is power_on_V3D() in v3d_device.c.
 */

#include "v3d_hw.h"
#include "v3d_regs.h"
#include "v3d_debug.h"

static void v3d_invalidate_l2t(void)
{
    V3D_L2TFLSTA = 0;
    V3D_L2TFLEND = ~0;
    V3D_L2TCACTL = (V3D_L2TCACTL_L2TFLS | V3D_L2TCACTL_FLM_FLUSH);
}

static void v3d_invalidate_slices(void)
{
    /* MESA-exact: the kernel's v3d_invalidate_slices writes exactly the
     * four 4-bit cache-select fields (TVCCS 27:24, TDCCS 19:16, UCC 11:8,
     * ICC 3:0) = 0x0f0f0f0f and never the reserved bits. Byte-order
     * symmetric, so no LE32. */
    V3D_SLCACTL = 0x0f0f0f0fUL;
}

void v3d_invalidate_caches(void)
{
    v3d_invalidate_l2t();
    v3d_invalidate_slices();
}

v3d_u8 v3d_get_binning_flush_count(void)
{
    return (v3d_u8)(LE32(V3D_BFC) & V3D_BFC_FLUSH_COUNT_MASK);
}

v3d_u8 v3d_get_render_frame_count(void)
{
    return (v3d_u8)(LE32(V3D_RFC) & V3D_RFC_FRAME_COUNT_MASK);
}

v3d_wait_result v3d_wait_for_binning_flush(v3d_u8 lastFlush)
{
    v3d_u8 currentFlushCount;
    v3d_u32 status;

    currentFlushCount = v3d_get_binning_flush_count();
    /* Handle wrap-around */
    while (currentFlushCount <= lastFlush && !(currentFlushCount == 0 && lastFlush == 255))
    {
        currentFlushCount = v3d_get_binning_flush_count();
        status = V3D_CT0CS;
        if (status & V3D_CTNCS_CTERR)
        {
            /* The control list executor has encountered some error */
            return v3d_wait_result_error_detected;
        }
    }
    return v3d_wait_result_success;
}

v3d_wait_result v3d_wait_for_render_frame(v3d_u8 lastFrame)
{
    v3d_u8 currentFrameCount;
    v3d_u32 status;

    currentFrameCount = v3d_get_render_frame_count();
    /* Handle wrap-around */
    while (currentFrameCount <= lastFrame && !(currentFrameCount == 0 && lastFrame == 255))
    {
        currentFrameCount = v3d_get_render_frame_count();
        status = V3D_CT1CS;
        if (status & V3D_CTNCS_CTERR)
        {
            /* The control list executor has encountered some error */
            return v3d_wait_result_error_detected;
        }
    }
    return v3d_wait_result_success;
}

/* V3D_T0QTS_ENABLE (0x02000000, v3d_regs.h) is ORed into tileStateData, so the address must have that bit clear (no alignment guarantees it). Separately, aligning tileStateData to 64 is best to be safe. */
void v3d_start_binning_commands(v3d_address binningCommandListStart,
                                 v3d_address binningCommandListEnd, v3d_address tileAllocation,
                                 v3d_u32 tileAllocationSize, v3d_address tileStateData)
{
    /* MESA-exact: the kernel's v3d_bin_job_run starts every bin job with
     * "Clear out the overflow allocation, so we don't reuse the overflow
     * attached to a previous job" -- BPOS = 0 -- before programming
     * CT0QMA/QMS/QTS/QBA/QEA. */
    V3D_BPOS = 0;

    if (tileAllocation)
    {
        V3D_CT0QMA = LE32(tileAllocation);
        V3D_CT0QMS = LE32(tileAllocationSize);
    }
    if (tileStateData)
    {
        /* Note: Implies alignment! */
        V3D_CT0QTS = LE32((tileStateData | V3D_T0QTS_ENABLE));
    }

    V3D_CT0QBA = LE32(binningCommandListStart);
    V3D_CT0QEA = LE32(binningCommandListEnd);
}

void v3d_start_render_commands(v3d_address renderCommandListStart, v3d_address renderCommandListEnd)
{
    V3D_CT1QBA = LE32(renderCommandListStart);
    V3D_CT1QEA = LE32(renderCommandListEnd);
}

v3d_f187 v3d_float_to_f187(v3d_float floatToConvert)
{
    typedef union FloatToUint
    {
        v3d_float f;
        v3d_u32 u;
    } FloatToUint;
    FloatToUint converted;
    v3d_f187 final;

    converted.f = floatToConvert;
    final = (v3d_f187)(converted.u >> 16);
    return final;
}

void* v3d_get_aligned_address(void* addressToAlign, int desiredAlignmentPowerOf2)
{
    v3d_uintptr desiredAddress =
        ((v3d_uintptr)addressToAlign + desiredAlignmentPowerOf2 - 1) & ~(desiredAlignmentPowerOf2 - 1);
    return (void*)desiredAddress;
}

void v3d_buffer_write(v3d_static_buffer* buffer, void* data, v3d_uintptr dataSize)
{
    int numBytesFree = buffer->capacity - buffer->used;
    if (numBytesFree < (int)dataSize)
    {
        dataSize = numBytesFree;
    }
    V3D_memcpy(buffer->start + buffer->used, data, dataSize);
    buffer->used += dataSize;
}

void v3d_buffer_align(v3d_static_buffer* buffer, unsigned int alignment)
{
    void* desiredAddress = v3d_get_aligned_address((void*)(buffer->start + buffer->used), alignment);
    buffer->used = (int)((v3d_u8*)desiredAddress - buffer->start);
}

/*
 * Fixed-capacity bump allocator -- does NOT grow on overflow. The growable
 * append (v3d_cl_claim_grow, v3d_clbuf.c) is built on top of this, not
 * something this function does.
 */
/* Packet writers that claim through v3d_buffer_claim_memory directly
 * (V3D_BUFFER_ALLOC_OPERATION and friends, v3d_hw.h) do NOT go through the
 * growable v3d_cl_claim_grow wrapper, and most of them dereference the
 * claimed pointer unconditionally for every field after the first. That is
 * every claim on binning_buf/render_buf/tile_list_buf, whose fixed capacity
 * a frame with many draw calls can exceed; only state_buf's callers claim
 * through v3d_cl_claim_grow. So on overflow this hands back a throwaway
 * scratch buffer rather than NULL -- those unconditional field writes stay
 * memory-safe (writes into real, valid
 * memory, just not memory the GPU will ever read), and the sticky
 * buffer->overflowed flag lets gl_FramePresent (context.c) detect this
 * and drop the whole frame rather than submit a truncated/corrupt control
 * list. 512 bytes is comfortably larger than any single CL packet struct
 * this project defines. */
static v3d_u8 g_v3d_overflow_scratch[512];

void* v3d_buffer_claim_memory(v3d_static_buffer* buffer, v3d_uintptr dataSize)
{
    int numBytesFree;
    void* data;

    numBytesFree = buffer->capacity - buffer->used;
    if (numBytesFree < (int)dataSize)
    {
        D(("v3d_buffer_claim_memory: EXHAUSTED buffer=%lx capacity=%ld used=%ld requested=%ld\n",
           (ULONG)buffer, (LONG)buffer->capacity, (LONG)buffer->used, (LONG)dataSize));
        buffer->overflowed = 1;
        if (dataSize > sizeof(g_v3d_overflow_scratch))
            return 0; /* too large for the scratch fallback: NULL, with overflowed already set */
        return g_v3d_overflow_scratch;
    }
    data = buffer->start + buffer->used;
    buffer->used += dataSize;
    return data;
}

void* v3d_buffer_allocate(v3d_static_buffer* buffer, v3d_uintptr dataSize, unsigned int alignment)
{
    v3d_buffer_align(buffer, alignment);
    return v3d_buffer_claim_memory(buffer, dataSize);
}

v3d_bool v3d_buffer_out_of_memory(v3d_static_buffer* buffer)
{
    return (v3d_bool)(buffer->used >= buffer->capacity);
}

/*
 * -- Texture tiling --
 * v3d_store_tiled_image converts a linear pixel buffer into the V3D tiled
 * layout given by its tiling_format, for texture upload (v3d_texture.c).
 * The code below is copied verbatim from PoC/v3d_v3d.h.
 */
/** @file v3d_cpu_tiling.h
 *
 * Contains load/store functions common to both v3d and vc4.  The utile layout
 * stayed the same, though the way utiles get laid out has changed.
 */

static inline void v3d_load_utile(void* cpu, v3d_u32 cpu_stride, void* gpu, v3d_u32 gpu_stride)
{

	for (v3d_u32 gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride)
	{
		V3D_memcpy(cpu, (APTR)((ULONG)gpu + gpu_offset), gpu_stride);
        cpu = (APTR)((ULONG)cpu + cpu_stride);
	}
}

static inline void v3d_store_utile(void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride)
{

	for (v3d_u32 gpu_offset = 0; gpu_offset < 64; gpu_offset += gpu_stride)
	{
		V3D_memcpy((APTR)((ULONG)gpu + gpu_offset), cpu, gpu_stride);
        cpu = (APTR)((ULONG)cpu + cpu_stride);
	}
}

/** @file v3d_tiling.c
 *
 * Handles information about the V3D tiling formats, and loading and storing
 * from them.
 */

/** Return the width in pixels of a 64-byte microtile. */
v3d_u32 v3d_utile_width(int componentsPerPixel)
{
	switch (componentsPerPixel)
	{
		case 1:
		case 2:
			return 8;
		case 4:
		case 8:
			return 4;
		case 16:
			return 2;
		default:
			return 4;  // unknown componentsPerPixel: not expected
	}
}

/** Return the height in pixels of a 64-byte microtile. */
v3d_u32 v3d_utile_height(int componentsPerPixel)
{
	switch (componentsPerPixel)
	{
		case 1:
			return 8;
		case 2:
		case 4:
			return 4;
		case 8:
		case 16:
			return 2;
		default:
			return 4;  // unknown componentsPerPixel: not expected
	}
}

/**
 * Returns the byte address for a given pixel within a utile.
 *
 * Utiles are 64b blocks of pixels in raster order, with 32bpp being a 4x4
 * arrangement.
 */
static inline v3d_u32 v3d_get_utile_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 x, v3d_u32 y)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);

	// x and y must lie within the utile: x < utile_w, y < v3d_utile_height(componentsPerPixel).

	return x * componentsPerPixel + y * utile_w * componentsPerPixel;
}

/**
 * Returns the byte offset for a given pixel in a LINEARTILE layout.
 *
 * LINEARTILE is a single line of utiles in either the X or Y direction.
 */
static inline v3d_u32 v3d_get_lt_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                          v3d_u32 x, v3d_u32 y)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 utile_index_x = x / utile_w;
	v3d_u32 utile_index_y = y / utile_h;

	// A single line of utiles: utile_index_x or utile_index_y must be 0.

	return (64 * (utile_index_x + utile_index_y) +
		    v3d_get_utile_pixel_offset(componentsPerPixel, x & (utile_w - 1), y & (utile_h - 1)));
}

/**
 * Returns the byte offset for a given pixel in a UBLINEAR layout.
 *
 * UBLINEAR is the layout where pixels are arranged in UIF blocks (2x2
 * utiles), and the UIF blocks are in 1 or 2 columns in raster order.
 */
static inline v3d_u32 v3d_get_ublinear_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 x,
	                                                v3d_u32 y, int ublinear_number)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 ub_w = utile_w * 2;
	v3d_u32 ub_h = utile_h * 2;
	v3d_u32 ub_x = x / ub_w;
	v3d_u32 ub_y = y / ub_h;

	return (256 * (ub_y * ublinear_number + ub_x) + ((x & utile_w) ? 64 : 0) +
		    ((y & utile_h) ? 128 : 0) +
		    +v3d_get_utile_pixel_offset(componentsPerPixel, x & (utile_w - 1), y & (utile_h - 1)));
}

static inline v3d_u32 v3d_get_ublinear_2_column_pixel_offset(v3d_u32 componentsPerPixel,
	                                                         v3d_u32 image_h, v3d_u32 x, v3d_u32 y)
{
	return v3d_get_ublinear_pixel_offset(componentsPerPixel, x, y, 2);
}

static inline v3d_u32 v3d_get_ublinear_1_column_pixel_offset(v3d_u32 componentsPerPixel,
	                                                         v3d_u32 image_h, v3d_u32 x, v3d_u32 y)
{
	return v3d_get_ublinear_pixel_offset(componentsPerPixel, x, y, 1);
}

// The below two functions are from Mesa/src/util/bitscan.c:
#ifndef V3D_FFS
#define V3D_FFS
#ifdef HAVE___BUILTIN_FFS
#elif defined(_MSC_VER) && (_M_IX86 || _M_ARM || _M_AMD64 || _M_IA64)
#else
int
v3d_ffs(int i)
{
	int bit = 0;
	if (!i)
		return bit;
	if (!(i & 0xffff)) {
		bit += 16;
		i >>= 16;
	}
	if (!(i & 0xff)) {
		bit += 8;
		i >>= 8;
	}
	if (!(i & 0xf)) {
		bit += 4;
		i >>= 4;
	}
	if (!(i & 0x3)) {
		bit += 2;
		i >>= 2;
	}
	if (!(i & 0x1))
		bit += 1;
	return bit + 1;
}
#endif

#ifdef HAVE___BUILTIN_FFSLL
#elif defined(_MSC_VER) && (_M_AMD64 || _M_ARM64 || _M_IA64)
#else
int
v3d_ffsll(long long int val)
{
	int bit;

	bit = v3d_ffs((unsigned) (val & 0xffffffff));
	if (bit != 0)
		return bit;

	bit = v3d_ffs((unsigned) (val >> 32));
	if (bit != 0)
		return 32 + bit;

	return 0;
}
#endif
#endif // V3D_FFS

/**
 * Returns the byte offset for a given pixel in a UIF layout.
 *
 * UIF is the general V3D tiling layout shared across 3D, media, and scanout.
 * It stores pixels in UIF blocks (2x2 utiles).
 */
static inline v3d_u32 v3d_get_uif_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                           v3d_u32 x, v3d_u32 y, v3d_bool do_xor)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 mb_width = utile_w * 2;
	v3d_u32 mb_height = utile_h * 2;
	v3d_u32 log2_mb_width = v3d_ffs(mb_width) - 1;
	v3d_u32 log2_mb_height = v3d_ffs(mb_height) - 1;

	/* Macroblock X, y */
	v3d_u32 mb_x = x >> log2_mb_width;
	v3d_u32 mb_y = y >> log2_mb_height;
	/* X, y within the macroblock */
	v3d_u32 mb_pixel_x = x - (mb_x << log2_mb_width);
	v3d_u32 mb_pixel_y = y - (mb_y << log2_mb_height);

	if (do_xor && (mb_x / 4) & 1)
		mb_y ^= 0x10;

	v3d_u32 mb_h = V3D_ALIGN(image_h, 1 << log2_mb_height) >> log2_mb_height;
	v3d_u32 mb_id = ((mb_x / 4) * ((mb_h - 1) * 4)) + mb_x + mb_y * 4;

	v3d_u32 mb_base_addr = mb_id * 256;

	v3d_bool top = mb_pixel_y < utile_h;
	v3d_bool left = mb_pixel_x < utile_w;

	/* Docs have this in pixels, we do bytes here. */
	v3d_u32 mb_tile_offset = (!top * 128 + !left * 64);

	v3d_u32 utile_x = mb_pixel_x & (utile_w - 1);
	v3d_u32 utile_y = mb_pixel_y & (utile_h - 1);

	v3d_u32 mb_pixel_address = (mb_base_addr + mb_tile_offset +
		                        v3d_get_utile_pixel_offset(componentsPerPixel, utile_x, utile_y));

	return mb_pixel_address;
}

static inline v3d_u32 v3d_get_uif_xor_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                               v3d_u32 x, v3d_u32 y)
{
	return v3d_get_uif_pixel_offset(componentsPerPixel, image_h, x, y, TRUE);
}

static inline v3d_u32 v3d_get_uif_no_xor_pixel_offset(v3d_u32 componentsPerPixel, v3d_u32 image_h,
	                                                  v3d_u32 x, v3d_u32 y)
{
	return v3d_get_uif_pixel_offset(componentsPerPixel, image_h, x, y, FALSE);
}

/* Loads/stores non-utile-aligned boxes by walking over the destination
 * rectangle, computing the address on the GPU, and storing/loading a pixel at
 * a time.
 */
static inline void v3d_move_pixels_unaligned(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	for (v3d_u32 y = 0; y < box->height; y++)
	{
		void* cpu_row = (APTR)((ULONG)cpu + y * cpu_stride);

		for (int x = 0; x < box->width; x++)
		{
			v3d_u32 pixel_offset =
				get_pixel_offset(componentsPerPixel, image_h, box->x + x, box->y + y);

			if (is_load)
			{
				V3D_memcpy((APTR)((ULONG)cpu_row + x * componentsPerPixel), (APTR)((ULONG)gpu + pixel_offset),
					       componentsPerPixel);
			}
			else
			{
				V3D_memcpy((APTR)((ULONG)gpu + pixel_offset), (APTR)((ULONG)cpu_row + x * componentsPerPixel),
					       componentsPerPixel);
			}
		}
	}
}

/* Breaks the image down into utiles and calls either the fast whole-utile
 * load/store functions, or the unaligned fallback case.
 */
static inline void v3d_move_pixels_general_percomponentsPerPixel(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	v3d_u32 utile_w = v3d_utile_width(componentsPerPixel);
	v3d_u32 utile_h = v3d_utile_height(componentsPerPixel);
	v3d_u32 utile_gpu_stride = utile_w * componentsPerPixel;
	v3d_u32 x1 = box->x;
	v3d_u32 y1 = box->y;
	v3d_u32 x2 = box->x + box->width;
	v3d_u32 y2 = box->y + box->height;
	v3d_u32 align_x1 = V3D_ALIGN(x1, utile_w);
	v3d_u32 align_y1 = V3D_ALIGN(y1, utile_h);
	v3d_u32 align_x2 = x2 & ~(utile_w - 1);
	v3d_u32 align_y2 = y2 & ~(utile_h - 1);

	/* Load/store all the whole utiles first. */
	for (v3d_u32 y = align_y1; y < align_y2; y += utile_h)
	{
		void* cpu_row = (APTR)((ULONG)cpu + (y - box->y) * cpu_stride);

		for (v3d_u32 x = align_x1; x < align_x2; x += utile_w)
		{
			void* utile_gpu = (APTR)((ULONG)gpu + get_pixel_offset(componentsPerPixel, image_h, x, y));
			void* utile_cpu = (APTR)((ULONG)cpu_row + (x - box->x) * componentsPerPixel);

			if (is_load)
			{
				v3d_load_utile(utile_cpu, cpu_stride, utile_gpu, utile_gpu_stride);
			}
			else
			{
				v3d_store_utile(utile_gpu, utile_gpu_stride, utile_cpu, cpu_stride);
			}
		}
	}

	/* If there were no aligned utiles in the middle, load/store the whole
	 * thing unaligned.
	 */
	if (align_y2 <= align_y1 || align_x2 <= align_x1)
	{
		v3d_move_pixels_unaligned(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
			                      box, get_pixel_offset, is_load);
		return;
	}

	/* Load/store the partial utiles. */
	v3d_texture_box partial_boxes[4] = {
		/* Top */
		{
		    .x = x1,
		    .width = x2 - x1,
		    .y = y1,
		    .height = align_y1 - y1,
		},
		/* Bottom */
		{
		    .x = x1,
		    .width = x2 - x1,
		    .y = align_y2,
		    .height = y2 - align_y2,
		},
		/* Left */
		{
		    .x = x1,
		    .width = align_x1 - x1,
		    .y = align_y1,
		    .height = align_y2 - align_y1,
		},
		/* Right */
		{
		    .x = align_x2,
		    .width = x2 - align_x2,
		    .y = align_y1,
		    .height = align_y2 - align_y1,
		},
	};
	for (int i = 0; i < V3D_ARRAY_SIZE(partial_boxes); i++)
	{
		void* partial_cpu = (APTR)((ULONG)cpu + (partial_boxes[i].y - y1) * cpu_stride +
			                 (partial_boxes[i].x - x1) * componentsPerPixel);

		v3d_move_pixels_unaligned(gpu, gpu_stride, partial_cpu, cpu_stride, componentsPerPixel,
			                      image_h, &partial_boxes[i], get_pixel_offset, is_load);
	}
}

static inline void v3d_move_pixels_general(
	void* gpu, v3d_u32 gpu_stride, void* cpu, v3d_u32 cpu_stride, int componentsPerPixel,
	v3d_u32 image_h, const v3d_texture_box* box,
	v3d_u32 (*get_pixel_offset)(v3d_u32 componentsPerPixel, v3d_u32 image_h, v3d_u32 x, v3d_u32 y),
	v3d_bool is_load)
{
	switch (componentsPerPixel)
	{
		case 1:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 1,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 2:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 2,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 4:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 4,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 8:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 8,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
		case 16:
			v3d_move_pixels_general_percomponentsPerPixel(gpu, gpu_stride, cpu, cpu_stride, 16,
				                                          image_h, box, get_pixel_offset, is_load);
			break;
	}
}

static inline void v3d_move_tiled_image(void* gpu, v3d_u32 gpu_stride, void* cpu,
	                                    v3d_u32 cpu_stride, enum v3d_memory_format tiling_format,
	                                    int componentsPerPixel, v3d_u32 image_h,
	                                    const v3d_texture_box* box, v3d_bool is_load)
{
	switch (tiling_format)
	{
		case V3D_MEMORY_FORMAT_UIF_XOR:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_uif_xor_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UIF_NO_XOR:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_uif_no_xor_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UB_LINEAR_2_UIF_BLOCKS_WIDE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_ublinear_2_column_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_UB_LINEAR_1_UIF_BLOCK_WIDE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_ublinear_1_column_pixel_offset, is_load);
			break;
		case V3D_MEMORY_FORMAT_LINEARTILE:
			v3d_move_pixels_general(gpu, gpu_stride, cpu, cpu_stride, componentsPerPixel, image_h,
				                    box, v3d_get_lt_pixel_offset, is_load);
			break;
		default:
			/* Unsupported tiling format: not expected; nothing is moved. */
			break;
	}
}

/**
 * Loads pixel data from the start (microtile-aligned) box in \p src to the
 * start of \p dst according to the given tiling format.
 */
void v3d_load_tiled_image(void* dst, v3d_u32 dst_stride, void* src, v3d_u32 src_stride,
	                      enum v3d_memory_format tiling_format, int componentsPerPixel,
	                      v3d_u32 image_h, const v3d_texture_box* box)
{
	v3d_move_tiled_image(src, src_stride, dst, dst_stride, tiling_format, componentsPerPixel,
		                 image_h, box, TRUE);
}

/**
 * Stores pixel data from the start of \p src into a (microtile-aligned) box in
 * \p dst according to the given tiling format.
 */
void v3d_store_tiled_image(void* dst, v3d_u32 dst_stride, void* src, v3d_u32 src_stride,
	                       enum v3d_memory_format tiling_format, int componentsPerPixel,
	                       v3d_u32 image_h, const v3d_texture_box* box)
{
	v3d_move_tiled_image(dst, dst_stride, src, src_stride, tiling_format, componentsPerPixel,
		                 image_h, box, FALSE);
}
