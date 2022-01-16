#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "gblcd.h"

#include "images.h"
#define IMAGE_MID_0	10
#define IMAGE_MID_1	11
#define IMAGE_LEFTOVER	12
#define LEFTOVER_COUNT	3

typedef union
{
	struct
	{
		uint8_t hour_t;
		uint8_t hour_v;
		uint8_t min_t;
		uint8_t min_v;
		uint8_t sec_t;
		uint8_t sec_v;
	};
	uint8_t raw[6];
} stored_time_t;

uint8_t fb[GBLCD_BUFSIZE * GBLCD_SCREEN_COUNT];

static stored_time_t time_old;
static stored_time_t time_now;
static uint32_t mid_time;

static uint32_t msg_time = 30;
static uint32_t msg_idx;

// digit index table
// choose which LCD shows which digit
static const uint8_t digit_tab[] =
{
	1, // hours, tenths
	2, // hours
	4, // minutes, tenths
	5, // minutes
	7, // seconds, tenths
	8, // seconds
	3, // separator
	6, // separator
	0, // 'leftover'
};

//
// funcs

static void add_image(uint32_t fbi, uint32_t idx, int32_t xoffs)
{
	const uint8_t *ftab;
	const uint8_t *src;
	uint8_t *dst;
	uint32_t xlen;

	src = image_data + idx * IMAGE_SIZE;
	dst = fb + fbi * GBLCD_BUFSIZE;

	if(xoffs < 0)
	{
		src += -xoffs;
		xlen = GBLCD_STRIDE + xoffs;
	} else
	{
		dst += xoffs;
		xlen = GBLCD_STRIDE - xoffs;
	}

	for(uint32_t y = 0; y < GBLCD_HEIGHT; y++)
	{
		for(uint32_t x = 0; x < xlen; x++)
			*dst++ = *src++;
		src += GBLCD_STRIDE - xlen;
		dst += GBLCD_STRIDE - xlen;
	}
}

static int check_time()
{
	struct tm *tm;
	time_t t;

	// get local time
	t = time(NULL);
	tm = localtime(&t);

	// convert to digits
	time_now.hour_t = tm->tm_hour / 10;
	time_now.hour_v = tm->tm_hour % 10;

	time_now.min_t = tm->tm_min / 10;
	time_now.min_v = tm->tm_min % 10;

	time_now.sec_t = tm->tm_sec / 10;
	time_now.sec_v = tm->tm_sec % 10;

	// check for updates
	return memcmp(time_now.raw, time_old.raw, sizeof(time_now));
}

//
// MAIN

int main(int argc, void **argv)
{
	if(gblcd_init("/dev/fb0"))
	{
		printf("- failed init framebuffer\n");
		return 1;
	}

	// prepare the screen
	check_time();
	time_old = time_now;
	for(uint32_t i = 0; i < sizeof(stored_time_t); i++)
		add_image(digit_tab[i], time_now.raw[i], 0);

	add_image(digit_tab[6], IMAGE_MID_0, 0);
	add_image(digit_tab[7], IMAGE_MID_0, 0);
	add_image(digit_tab[8], IMAGE_LEFTOVER, 0);

	gblcd_update(fb);

	while(1)
	{
		if(check_time())
		{
			int32_t x;

			// funny messages
			if(msg_time)
			{
				msg_time--;
				if(!msg_time)
				{
					msg_idx++;
					add_image(digit_tab[8], IMAGE_LEFTOVER + msg_idx, 0);
					if(msg_idx < LEFTOVER_COUNT)
						msg_time = 6;
				}
			}

			// separator
			mid_time = 0;
			add_image(digit_tab[6], IMAGE_MID_0, 0);
			add_image(digit_tab[7], IMAGE_MID_0, 0);

			// animate digits
			x = 0;
			while(x < GBLCD_STRIDE)
			{
				x += 4;
				if(x > GBLCD_STRIDE)
					x = GBLCD_STRIDE;

				for(uint32_t i = 0; i < sizeof(stored_time_t); i++)
				{
					if(time_old.raw[i] != time_now.raw[i])
					{
						add_image(digit_tab[i], time_old.raw[i], -x);
						add_image(digit_tab[i], time_now.raw[i], GBLCD_STRIDE - x);
					}
				}

				gblcd_update(fb);
				usleep(30 * 1000);
			}

			// update time
			time_old = time_now;
		} else
		{
			mid_time++;
			if(mid_time == 5)
			{
				add_image(digit_tab[6], IMAGE_MID_1, 0);
				add_image(digit_tab[7], IMAGE_MID_1, 0);
				gblcd_update(fb);
			}
		}

		usleep(50 * 1000);
	}

	return 0;
}

