/*
 *  Generic fillrect for frame buffers with packed pixels of any depth. 
 *
 *      Copyright (C)  2000 James Simmons (jsimmons@linux-fbdev.org) 
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  The code for depths like 24 that don't have integer number of pixels per 
 *  long is broken and needs to be fixed. For now I turned these types of 
 *  mode off.
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

#if BITS_PER_LONG == 32
#define FB_READ		fb_readl
#define FB_WRITE	fb_writel
#else
#define FB_READ		fb_readq
#define FB_WRITE	fb_writeq
#endif

void cfb_fillrect(struct fb_info *p, struct fb_fillrect *rect)
{
	unsigned long start_index, end_index, start_mask = 0, end_mask = 0;
	unsigned long height, ppw, fg, fgcolor;
	int i, n, x2, y2, linesize = p->fix.line_length;
	int bpl = sizeof(unsigned long);
	unsigned long *dst = NULL;
	char *dst1;

	if (!rect->width || !rect->height)
		return;

	/* We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly. */
	x2 = rect->dx + rect->width;
	y2 = rect->dy + rect->height;
	x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
	y2 = y2 < p->var.yres_virtual ? y2 : p->var.yres_virtual;
	rect->width = x2 - rect->dx;
	height = y2 - rect->dy;

	/* Size of the scanline in bytes */
	n = (rect->width * (p->var.bits_per_pixel >> 3));
	ppw = BITS_PER_LONG / p->var.bits_per_pixel;

	dst1 = p->screen_base + (rect->dy * linesize) +
	    (rect->dx * (p->var.bits_per_pixel >> 3));
	start_index = ((unsigned long) dst1 & (bpl - 1));
	end_index = ((unsigned long) (dst1 + n) & (bpl - 1));

	if (p->fix.visual == FB_VISUAL_TRUECOLOR)
		fg = fgcolor = ((u32 *) (p->pseudo_palette))[rect->color];
	else
		fg = fgcolor = rect->color;

	for (i = 0; i < ppw - 1; i++) {
		fg <<= p->var.bits_per_pixel;
		fg |= fgcolor;
	}

	if (start_index) {
		start_mask = fg << (start_index << 3);
		n -= (bpl - start_index);
	}

	if (end_index) {
		end_mask = fg >> ((bpl - end_index) << 3);
		n -= end_index;
	}

	n = n / bpl;

	if (n <= 0) {
		if (start_mask) {
			if (end_mask)
				end_mask &= start_mask;
			else
				end_mask = start_mask;
			start_mask = 0;
		}
		n = 0;
	}

	if ((BITS_PER_LONG % p->var.bits_per_pixel) == 0) {
		switch (rect->rop) {
		case ROP_COPY:
			do {
				/* Word align to increases performace :-) */
				dst =
				    (unsigned long *) (dst1 - start_index);

				if (start_mask) {
					FB_WRITE(FB_READ(dst) |
						 start_mask, dst);
					dst++;
				}

				for (i = 0; i < n; i++) {
					FB_WRITE(fg, dst);
					dst++;
				}

				if (end_mask)
					FB_WRITE(FB_READ(dst) | end_mask,
						 dst);
				dst1 += linesize;
			} while (--height);
			break;
		case ROP_XOR:
			do {
				dst =
				    (unsigned long *) (dst1 - start_index);

				if (start_mask) {
					FB_WRITE(FB_READ(dst) ^
						 start_mask, dst);
					dst++;
				}

				for (i = 0; i < n; i++) {
					FB_WRITE(FB_READ(dst) ^ fg, dst);
					dst++;
				}

				if (end_mask) {
					FB_WRITE(FB_READ(dst) ^ end_mask,
						 dst);
				}
				dst1 += linesize;
			} while (--height);
			break;
		}
	} else {
		/* 
		 * Slow Method:  The aim is to find the number of pixels to
		 * pack in order to write doubleword multiple data.
		 * For 24 bpp, 4 pixels are packed which are written as 
		 * 3 dwords.
		 */
		char *dst2, *dst3;
		int bytes = (p->var.bits_per_pixel + 7) >> 3;
		int read, write, total, pack_size;
		u32 pixarray[BITS_PER_LONG >> 3], m;

		fg = fgcolor;
		read = (bytes + (bpl - 1)) & ~(bpl - 1);
		write = bytes;
		total = (rect->width * bytes);

		pack_size = bpl * write;

		dst3 = (char *) pixarray;

		for (n = read; n--;) {
			*(u32 *) dst3 = fg;
			dst3 += bytes;
		}

		switch (rect->rop) {
		case ROP_COPY:
			do {
				dst2 = dst1;
				n = total;

				while (n >= pack_size) {
					for (m = 0; m < write; m++) {
						fb_writel(pixarray[m],
							  (u32 *) dst2);
						dst2 += 4;
					}
					n -= pack_size;
				}
				if (n) {
					m = 0;
					while (n--)
						fb_writeb(((u8 *)
							   pixarray)[m++],
							  dst2++);
				}
				dst1 += linesize;
			} while (--height);
			break;
		case ROP_XOR:
			do {
				dst2 = dst1;
				n = total;

				while (n >= pack_size) {
					for (m = 0; m < write; m++) {
						fb_writel(fb_readl
							  ((u32 *) dst2) ^
							  pixarray[m],
							  (u32 *) dst2);
						dst2 += 4;
					}
					n -= pack_size;
				}
				if (n) {
					m = 0;
					while (n--) {
						fb_writeb(fb_readb(dst2) ^
							  ((u8 *)
							   pixarray)[m++],
							  dst2);
						dst2++;
					}
				}
				dst1 += linesize;
			} while (--height);
			break;
		}

	}
	return;
}
