// kgsws' GameBoy LCD output
// LCD timing was modified to save DMA RAM.
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "soc/timer_group_struct.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "soc/gpio_periph.h"
#include "soc/lldesc.h"
#include "freertos/queue.h"

#include "gblcd_output.h"

#define GBLCD_LINE_WIDTH	432	// clock cycles; must be divisible by 4
#define GBLCD_LINE_START	12	// uint32_t steps
#define GBLCD_FRAME_HEIGHT	154	// lines

static lldesc_t *dma;
static void *line;
static void *line_wr;
static uint32_t lcd_y;
static uint32_t lcd_frame;
static uint8_t *lcd_buff;
static uint8_t *lcd_ptr;
static uint8_t *lcd_swap;

static uint8_t framebuffers[GBLCD_BUFFER_SIZE * 2];
static QueueHandle_t event_queue;

/// BIT MAPPING
// 0 - LD0
// 1 - LD1
// 2 - CP
// 3 - ST
// 4 - S
// 5 - FR
// 6 - CPL
// 7 - CPG

//
// IRQ

static void IRAM_ATTR prepare_line_isr(void *arg)
{
	uint32_t *ptr;
	uint32_t base;

	I2S1.int_clr.out_eof = 1;

	//// NEW LINE
	ptr = line_wr;

	// vsync
	if(lcd_y)
		base = 0;
	else
		base = 0x10101010;

	// FR
	if((lcd_frame ^ lcd_y) & 1)
		base |= 0x20202020;

	// generate pre-pixel data
	for(uint32_t i = 0; i < GBLCD_LINE_START; i++)
	{
		*ptr = base;
		ptr++;
	}

	if(lcd_y < GBLCD_HEIGHT)
	{
		// generate clock and pixels
		for(uint32_t i = 0; i < GBLCD_WIDTH / 4; i++)
		{
			register uint32_t tmp;
			register uint_fast8_t px = *lcd_ptr++;

			// two clock pulses
			tmp = base | 0x00040004;

			// add 2 pixels; 2bpp each
			tmp |= (uint32_t)(px & 0b00000011) << 16;
			tmp |= (uint32_t)(px & 0b00000011) << 24;
			tmp |= (uint32_t)(px & 0b00001100) >> 2;
			tmp |= (uint32_t)(px & 0b00001100) << 6;

			// next
			*ptr = tmp;
			ptr++;

			// two clock pulses
			tmp = base | 0x00040004;

			// add 2 pixels; 2bpp each
			tmp |= (uint32_t)(px & 0b00110000) << 12;
			tmp |= (uint32_t)(px & 0b00110000) << 20;
			tmp |= (uint32_t)(px & 0b11000000) >> 6;
			tmp |= (uint32_t)(px & 0b11000000) << 2;

			// next
			*ptr = tmp;
			ptr++;
		}
	}

	// generate post-pixel data
	while((void*)ptr < line_wr + GBLCD_LINE_WIDTH)
	{
		*ptr = base;
		ptr++;
	}

	// extra stuff
	ptr = line_wr;

	// CPL + CPG
	ptr[(GBLCD_LINE_WIDTH / 4) - 1] = base | 0xC0C08080;

	// CPG
	ptr[0] = base | 0x00800000;
	ptr[1] = base | 0x80800000;
	ptr[50] |= 0x80800000;
	ptr[70] |= 0x80800000;

	// hsync
	if(lcd_y < GBLCD_HEIGHT)
	{
		ptr[GBLCD_LINE_START-1] = base | 0x00000800;
		ptr[GBLCD_LINE_START] |= 0x00080000;
	}

	// next line
	lcd_y++;
	if(lcd_y >= GBLCD_FRAME_HEIGHT)
		lcd_y = 0;
	if(lcd_y == GBLCD_HEIGHT)
	{
		lcd_frame++;
		if(lcd_swap)
		{
			lcd_buff = lcd_swap;
			lcd_swap = NULL;
			xQueueSendFromISR(event_queue, (void*)lcd_ptr, NULL);
		}
		lcd_ptr = lcd_buff;
	}

	// swap DMA pointer
	if(line_wr == line)
		line_wr += GBLCD_LINE_WIDTH;
	else
		line_wr -= GBLCD_LINE_WIDTH;
}

//
// API

uint8_t *gblcd_output_init(void *framebuffer)
{
	/// I2S (LCD output mode)

	lcd_buff = framebuffers;
	lcd_ptr = framebuffers;

	line = heap_caps_malloc(GBLCD_LINE_WIDTH * 2, MALLOC_CAP_DMA);
	dma = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t) * 2, MALLOC_CAP_DMA);

	printf("[GB_in] line %p dma %p\n", line, dma);

	memset(line, 0, GBLCD_LINE_WIDTH * 2);

	dma[0].offset = 0;
	dma[0].size = GBLCD_LINE_WIDTH;
	dma[0].length = GBLCD_LINE_WIDTH;
	dma[0].sosf = 0;
	dma[0].eof = 1;
	dma[0].owner = 1;
	dma[0].buf = line;
	dma[0].empty = (uint32_t)&dma[1];

	dma[1].offset = 0;
	dma[1].size = GBLCD_LINE_WIDTH;
	dma[1].length = GBLCD_LINE_WIDTH;
	dma[1].sosf = 0;
	dma[1].eof = 1;
	dma[1].owner = 1;
	dma[1].buf = line + GBLCD_LINE_WIDTH;
	dma[1].empty = (uint32_t)&dma[0];

	//
	periph_module_enable(PERIPH_I2S1_MODULE);

	// init

	I2S1.conf.tx_reset = 1;
	I2S1.conf.tx_reset = 0;
	I2S1.conf.tx_fifo_reset = 1;
	I2S1.conf.tx_fifo_reset = 0;

	I2S1.conf2.val = 0;
	I2S1.conf2.lcd_en = 1;
	I2S1.conf2.lcd_tx_wrx2_en = 1;

	I2S1.sample_rate_conf.val = 0;
	I2S1.sample_rate_conf.rx_bits_mod = 8;
	I2S1.sample_rate_conf.tx_bits_mod = 8;
	I2S1.sample_rate_conf.rx_bck_div_num = 2;
	I2S1.sample_rate_conf.tx_bck_div_num = 2;

	I2S1.clkm_conf.val = 0;
	I2S1.clkm_conf.clka_en = 0;
	I2S1.clkm_conf.clkm_div_a = 0;
	I2S1.clkm_conf.clkm_div_b = 0;
	I2S1.clkm_conf.clkm_div_num = 20;

	I2S1.conf1.val = 0;
	I2S1.conf1.tx_pcm_bypass = 1;

	I2S1.conf_chan.tx_chan_mod = 1;
	I2S1.conf_chan.rx_chan_mod = 1;

	I2S1.lc_conf.in_rst = 1;
	I2S1.lc_conf.out_rst = 1;
	I2S1.lc_conf.ahbm_rst = 1;
	I2S1.lc_conf.ahbm_fifo_rst = 1;
	I2S1.lc_conf.in_rst = 0;
	I2S1.lc_conf.out_rst = 0;
	I2S1.lc_conf.ahbm_rst = 0;
	I2S1.lc_conf.ahbm_fifo_rst = 0;

	I2S1.conf.tx_reset = 1;
	I2S1.conf.tx_fifo_reset = 1;
	I2S1.conf.rx_fifo_reset = 1;
	I2S1.conf.tx_reset = 0;
	I2S1.conf.tx_fifo_reset = 0;
	I2S1.conf.rx_fifo_reset = 0;

	I2S1.timing.val = 0;

	I2S1.lc_conf.val = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN | I2S_OUT_DATA_BURST_EN;

	// pins
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_LD0], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_LD0, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_LD0, I2S1O_DATA_OUT0_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_LD1], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_LD1, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_LD1, I2S1O_DATA_OUT1_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_CP], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_CP, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_CP, I2S1O_DATA_OUT2_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_ST], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_ST, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_ST, I2S1O_DATA_OUT3_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_S], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_S, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_S, I2S1O_DATA_OUT4_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_FR], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_FR, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_FR, I2S1O_DATA_OUT5_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_CPL], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_CPL, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_CPL, I2S1O_DATA_OUT6_IDX, 0, 0);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_OUT_CPG], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_OUT_CPG, GPIO_MODE_OUTPUT);
	gpio_matrix_out(GBLCD_OUT_CPG, I2S1O_DATA_OUT7_IDX, 0, 0);

	// queue
	event_queue = xQueueCreate(1, sizeof(void*));

	// start
	line_wr = line;
	esp_intr_alloc(ETS_I2S1_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1, prepare_line_isr, NULL, NULL);

	I2S1.int_clr.out_eof = 1;
	I2S1.int_ena.out_eof = 1;
	I2S1.out_link.addr = ((uint32_t)dma) & 0xfffff;
	I2S1.out_link.start = 1;
	I2S1.conf.tx_start = 1;

	return framebuffers + GBLCD_BUFFER_SIZE;
}

uint8_t *gblcd_swap_buffers()
{
	static uint8_t *fb_set;
	uint8_t *fb_now = lcd_buff;

	if(fb_now == framebuffers)
		fb_set = framebuffers + GBLCD_BUFFER_SIZE;
	else
		fb_set = framebuffers;

	lcd_swap = fb_set;

	xQueueReceive(event_queue, (void*)&fb_set, portMAX_DELAY);

	return fb_now;
}

void gblcd_wait_vblank()
{
	static uint32_t dummy;
	lcd_swap = lcd_buff;
	xQueueReceive(event_queue, (void*)&dummy, portMAX_DELAY);
}

