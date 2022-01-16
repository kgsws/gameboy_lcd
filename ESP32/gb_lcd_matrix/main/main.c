#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bootloader_random.h"
#include "gblcd_output.h"

#include "glyphmap.h"

#define MAX_MATROWS	20

typedef struct
{
	uint8_t tickrate;
	uint8_t tick;
	uint8_t face_tick;
	uint8_t x;
	uint16_t y;
	uint8_t fade_rate;
	uint8_t face;
	uint8_t face_start;
} matrix_row_t;

static uint8_t *fb;
matrix_row_t matrows[MAX_MATROWS];

//
// glyph instertion

static void put_glyph(uint32_t x, uint32_t y, uint32_t idx, uint32_t fade)
{
	// too lazy, X is a byte index, not a pixel
	// this is just a simple demo anyway
	uint8_t *dst;
	const uint8_t *src;
	uint32_t ymax;

	if(y >= GBLCD_HEIGHT)
		return;

	idx *= GLYPH_WIDTH / 4;

	dst = fb + x + y * (GBLCD_WIDTH / 4);
	src = glyphmap + (idx % (GLYPH_MAP_WIDTH / 4));
	src += ((GLYPH_MAP_WIDTH * GLYPH_MAP_ROW) / 4) * (idx / (GLYPH_MAP_WIDTH / 4));

	ymax = GLYPH_MAP_ROW;
	if(y > GBLCD_HEIGHT - GLYPH_MAP_ROW)
		ymax -= y - (GBLCD_HEIGHT - GLYPH_MAP_ROW);

	for(uint32_t y = 0; y < ymax; y++)
	{
		for(uint32_t x = 0; x < GLYPH_WIDTH / 4; x++)
		{
			*dst |= fadetable[*src + fade * 256];
			dst++;
			src++;
		}
		dst += (GBLCD_WIDTH - GLYPH_WIDTH) / 4;
		src += (GLYPH_MAP_WIDTH - GLYPH_WIDTH) / 4;
	}
}

static void add_glyph(uint32_t slot)
{
	union
	{
		struct
		{
			uint32_t face_start : 8;
			uint32_t y : 2;
			uint32_t face : 6;
			uint32_t tickrate : 2;
			uint32_t fade : 3;
		};
		uint32_t raw;
	} rng;

	// randomize
	rng.raw = esp_random();

	matrows[slot].tickrate = 1 + rng.tickrate;
	matrows[slot].tick = 0;
//	matrows[slot].face_tick = 0; // intentionally skipped
	matrows[slot].x = slot * 2;
	matrows[slot].y = 1 + rng.y;
	matrows[slot].fade_rate = 3 + rng.fade;
	matrows[slot].face = rng.face % GLYPH_COUNT;
	matrows[slot].face_start = rng.face_start;
}

static void project_row(matrix_row_t *mr)
{
	uint_fast8_t is_visible = 0;
	int32_t tail_y;
	int32_t tail_f;
	uint32_t tail_glypth;

	if(!mr->tickrate)
		return;

	// tail
	tail_y = ((mr->y - 1) / 12) * 12;
	tail_f = 1 << 4;
	tail_glypth = mr->face_start;
	while(1)
	{
		tail_f += mr->fade_rate;

		if(tail_y < 0)
			break;
		if(tail_f >= (3 << 4))
			break;

		if(tail_y < GBLCD_HEIGHT)
			is_visible = 1;

		put_glyph(mr->x, tail_y, tail_glypth % GLYPH_COUNT, tail_f >> 4);

		tail_y -= 12;
		tail_glypth += mr->face_start;
	}

	if(!is_visible && mr->y >= GBLCD_HEIGHT)
	{
		add_glyph(mr - matrows);
		return;
	}

	// head
	put_glyph(mr->x, mr->y, mr->face, 0);

	// animate face
	mr->face_tick++;
	if(mr->face_tick >= 7)
	{
		mr->face_tick = 0;
		mr->face = (mr->x ^ mr->y ^ mr->tick) % GLYPH_COUNT;
	}

	// tick rate
	mr->tick++;
	if(mr->tick < mr->tickrate)
		return;
	mr->tick = 0;

	// animate movement
	mr->y++;
}

static void update_animation()
{
	memset(fb, 0, GBLCD_BUFFER_SIZE);
	for(uint32_t i = 0; i < MAX_MATROWS; i++)
		project_row(matrows + i);
}

static void prepare_logo()
{
	// works only with provided glyphmap
	static const uint8_t *logo = (void*)"by kgsws";
	const uint8_t *ptr = logo;
	uint32_t idx = 6;

	while(*ptr)
	{
		if(*ptr >= 'a' && *ptr <= 'z')
		{
			matrows[idx].face = *ptr - 'a';
			matrows[idx].y = 64;
			matrows[idx].fade_rate = 255;
		}
		idx++;
		ptr++;
	}
}

//
// animation task

static void matrix_task(void *unused)
{
	// randomize
	for(uint32_t i = 0; i < MAX_MATROWS; i++)
		add_glyph(i);
	prepare_logo();

	// init LCD
	fb = gblcd_output_init();

	// logo-frame
	update_animation();
	memset(fb, 0, (GBLCD_WIDTH * GLYPH_MAP_ROW) / 4);
	fb = gblcd_swap_buffers();
	vTaskDelay(4000 / portTICK_PERIOD_MS);

	// animate!
	while(1)
	{
		update_animation();
		fb = gblcd_swap_buffers();
	}
}

//
// entry

void app_main(void)
{
	printf("-= kgsws' GameBoy LCD animation =-\n");
	bootloader_random_enable();
	xTaskCreatePinnedToCore(&matrix_task, "matrix task", 2048, NULL, 5, NULL, 1);
}

