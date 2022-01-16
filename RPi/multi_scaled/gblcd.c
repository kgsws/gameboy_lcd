//
// kgsws' GameBoy LCD output
// This code uses parallel RGB as a fast GPIO for custom waveforms.
// Required framebuffer resolution:
//  408 x 307
//  6px HBlank 1 line VBlank
// Framebuffer contains two LCD frames. This is required as LCD waveforms differ between odd and even frames.
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "gblcd.h"

#define LINE_WIDTH_ACTUAL	408
#define LINE_WIDTH	416	// somehow this is 8px more than actual width (on RPi)
#define LINE_START	40
#define FRAME_HEIGHT	154

// LCD control lanes; Forced on RED channel by the code
// BEWARE! R6 and R7 are used as a data by the code
#define OUTBIT_CPG	(1 << 0)
#define OUTBIT_CPL	(1 << 1)
#define OUTBIT_ST	(1 << 2)
#define OUTBIT_CP	(1 << 3)
#define OUTBIT_FR	(1 << 4)
#define OUTBIT_S	(1 << 5)

#define BUFFERSIZE	(LINE_WIDTH * ((FRAME_HEIGHT*2)-1))
#define screensize	(BUFFERSIZE * sizeof(uint32_t))

static void *fbp;
static int fbfd;

// pre-calculated control waveform
static uint8_t sync_waveform[BUFFERSIZE];

//
// funcs

static void fill_line(uint8_t *dst, uint32_t lcd_y, uint32_t lcd_frame)
{
	uint8_t *ptr = dst;
	uint8_t base;

	// vsync
	if(lcd_y)
		base = 0;
	else
		base = OUTBIT_S;

	// FR
	if((lcd_frame ^ lcd_y) & 1)
		base |= OUTBIT_FR;

	// generate pre-pixel data
	for(uint32_t i = 0; i < LINE_START; i++)
	{
		*ptr = base;
		ptr++;
	}

	if(lcd_y < GBLCD_HEIGHT)
	{
		// generate clock and pixels
		for(uint32_t i = 0; i < GBLCD_WIDTH; i++)
		{
			// clock pulse
			*ptr = base | OUTBIT_CP;
			ptr++;
			// clock pulse
			*ptr = base;
			ptr++;
		}
	}

	// generate post-pixel data
	while(ptr < dst + LINE_WIDTH)
	{
		*ptr = base;
		ptr++;
	}

	// extra stuff
	if(lcd_y >= FRAME_HEIGHT - 1)
		// meh - just to keep symetry
		return;

	// CPL + CPG
	dst[LINE_WIDTH_ACTUAL-8] = base | OUTBIT_CPL | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-7] = base | OUTBIT_CPL | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-6] = base | OUTBIT_CPL | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-5] = base | OUTBIT_CPL | OUTBIT_CPG;

	// CPG
	dst[LINE_WIDTH_ACTUAL-4] = base | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-3] = base | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-2] = base | OUTBIT_CPG;
	dst[LINE_WIDTH_ACTUAL-1] = base | OUTBIT_CPG;

	if(lcd_y)
	{
		dst[0] = base | OUTBIT_CPG;
		dst[1] = base | OUTBIT_CPG;
		dst[2] = base | OUTBIT_CPG;
		dst[3] = base | OUTBIT_CPG;
	} else
	{
		// CPG
		dst[6] = base | OUTBIT_CPG;
		dst[7] = base | OUTBIT_CPG;

		dst[12] = base | OUTBIT_CPG;
		dst[13] = base | OUTBIT_CPG;
		dst[14] = base | OUTBIT_CPG;
		dst[15] = base | OUTBIT_CPG;

		base &= ~OUTBIT_S;

		// CPL + CPG
		dst[0] = base | OUTBIT_CPL | OUTBIT_CPG;
		dst[1] = base | OUTBIT_CPL | OUTBIT_CPG;
		dst[2] = base | OUTBIT_CPL | OUTBIT_CPG;
		dst[3] = base | OUTBIT_CPL | OUTBIT_CPG;

		// CPG
		dst[4] = base | OUTBIT_CPG;
		dst[5] = base | OUTBIT_CPG;

		dst[LINE_WIDTH_ACTUAL-2] = base | OUTBIT_CPG;
		dst[LINE_WIDTH_ACTUAL-1] = base | OUTBIT_CPG;
	}

	// CPG
	dst[182] |= OUTBIT_CPG;
	dst[183] |= OUTBIT_CPG;
	dst[184] |= OUTBIT_CPG;
	dst[185] |= OUTBIT_CPG;

	dst[300] |= OUTBIT_CPG;
	dst[301] |= OUTBIT_CPG;
	dst[302] |= OUTBIT_CPG;
	dst[303] |= OUTBIT_CPG;


	// hsync
	if(lcd_y < GBLCD_HEIGHT)
	{
		dst[LINE_START-1] |= OUTBIT_ST;
		dst[LINE_START] |= OUTBIT_ST;
	}
}

//
// API

int gblcd_init(const char *fbdev)
{
	uint8_t *ptr;
	uint32_t *dst;

	// open framebuffer
	fbfd = open(fbdev, O_RDWR);
	if(fbfd < 0)
	{
		return 1;
	}

	// map framebuffer
	fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if(fbp == MAP_FAILED)
	{
		close(fbfd);
		return 1;
	}

	// prepare timing signals
	ptr = sync_waveform;
	for(uint32_t i = 0; i < FRAME_HEIGHT; i++)
	{
		fill_line(ptr, i, 0);
		ptr += LINE_WIDTH;
	}
	for(uint32_t i = 0; i < FRAME_HEIGHT-1; i++)
	{
		fill_line(ptr, i, 1);
		ptr += LINE_WIDTH;
	}

	// clear the screen (copy only control waveforms)
	ptr = sync_waveform;
	dst = fbp;
	for(uint32_t i = 0; i < BUFFERSIZE; i++)
		*dst++ = *ptr++;

	return 0;
}

void gblcd_finish()
{
	munmap(fbp, screensize);
	close(fbfd);
	fbfd = -1;
}

void gblcd_update(uint8_t *buffer)
{
	uint32_t *dst0 = (uint32_t*)fbp + LINE_START;
	uint32_t *dst1 = dst0 + LINE_WIDTH * FRAME_HEIGHT;
	uint8_t *sync0 = sync_waveform + LINE_START;
	uint8_t *sync1 = sync0 + LINE_WIDTH * FRAME_HEIGHT;
	uint32_t offset = 0;

	for(uint32_t y = 0; y < GBLCD_HEIGHT; y++)
	{
		for(uint32_t x = 0; x < GBLCD_WIDTH / 4; x++)
		{
			uint32_t pixels;

			// A
			pixels = 0;
			for(uint32_t i = 0; i < GBLCD_SCREEN_COUNT; i++)
			{
				pixels <<= 2;
				pixels |= (uint32_t)(buffer[offset + i * GBLCD_BUFSIZE] & 0b00000011) << 6;
			}
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;

			// B
			pixels = 0;
			for(uint32_t i = 0; i < GBLCD_SCREEN_COUNT; i++)
			{
				pixels <<= 2;
				pixels |= (uint32_t)(buffer[offset + i * GBLCD_BUFSIZE] & 0b00001100) << 4;
			}
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;

			// B
			pixels = 0;
			for(uint32_t i = 0; i < GBLCD_SCREEN_COUNT; i++)
			{
				pixels <<= 2;
				pixels |= (uint32_t)(buffer[offset + i * GBLCD_BUFSIZE] & 0b00110000) << 2;
			}
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;

			// C
			pixels = 0;
			for(uint32_t i = 0; i < GBLCD_SCREEN_COUNT; i++)
			{
				pixels <<= 2;
				pixels |= (uint32_t)(buffer[offset + i * GBLCD_BUFSIZE] & 0b11000000) << 0;
			}
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;
			*dst0++ = pixels | *sync0++;
			*dst1++ = pixels | *sync1++;

			//
			offset++;
		}

		dst0 += LINE_WIDTH - GBLCD_WIDTH * 2;
		dst1 += LINE_WIDTH - GBLCD_WIDTH * 2;
		sync0 += LINE_WIDTH - GBLCD_WIDTH * 2;
		sync1 += LINE_WIDTH - GBLCD_WIDTH * 2;
	}
}

