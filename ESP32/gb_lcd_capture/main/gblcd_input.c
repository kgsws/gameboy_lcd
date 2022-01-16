#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/i2s_struct.h"
#include "soc/timer_group_struct.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "soc/gpio_periph.h"
#include "soc/lldesc.h"
#include "freertos/queue.h"

#include "gblcd_input.h"

#define GBLCD_SIZE_DMA	(GBLCD_SIZE * 2)
#define MAX_DMA	4092
#define DMA_COUNT	((GBLCD_SIZE_DMA + (MAX_DMA-1)) / MAX_DMA)

static lldesc_t *dma0;
static lldesc_t *dma1;
static void *frame;
static uint_fast8_t fb_idx;

QueueHandle_t event_queue;

//
// IRQ

static void IRAM_ATTR finish_frame_isr(void *arg)
{
	static void *ptr;

	typeof(TIMERG1.int_st_timers) status = TIMERG1.int_st_timers;

	if(status.val == 0)
		return;

	TIMERG1.int_clr_timers.val = status.val;

	if(status.t0)
	{
		// one extra clock cycle has to be faked here
		// seems like DMA is one uint32_t behind clock pulse
		gpio_matrix_in(0x38, I2S1I_WS_IN_IDX, false);
		gpio_matrix_in(GBLCD_INPUT_CP_ST, I2S1I_WS_IN_IDX, false);

		I2S1.conf.rx_start = 0;
		I2S1.in_link.stop = 1;
		I2S1.conf.rx_reset = 1;
		I2S1.conf.rx_reset = 0;

		TIMERG1.hw_timer[0].config.enable = 0;

		if(fb_idx)
			ptr = frame;
		else
			ptr = frame + GBLCD_SIZE_DMA;

		xQueueSendFromISR(event_queue, (void*)&ptr, NULL);

		gpio_intr_enable(GBLCD_INPUT_S);
	}
}

static void IRAM_ATTR capture_vsync_isr(void *arg)
{
	gpio_intr_disable(GBLCD_INPUT_S);

	I2S1.conf.rx_start = 0;
	I2S1.conf.rx_reset = 1;
	I2S1.conf.rx_reset = 0;
	I2S1.conf.rx_fifo_reset = 1;
	I2S1.conf.rx_fifo_reset = 0;
	I2S1.lc_conf.in_rst = 1;
	I2S1.lc_conf.in_rst = 0;
	I2S1.lc_conf.ahbm_fifo_rst = 1;
	I2S1.lc_conf.ahbm_fifo_rst = 0;
	I2S1.lc_conf.ahbm_rst = 1;
	I2S1.lc_conf.ahbm_rst = 0;

	I2S1.rx_eof_num = (GBLCD_SIZE_DMA / sizeof(uint32_t)) + 1;

	fb_idx ^= 1;
	if(fb_idx)
		I2S1.in_link.addr = ((uint32_t)dma0) & 0xfffff;
	else
		I2S1.in_link.addr = ((uint32_t)dma1) & 0xfffff;

	I2S1.in_link.start = 1;
	I2S1.conf.rx_start = 1;

	TIMERG1.hw_timer[0].reload = 1;
	TIMERG1.hw_timer[0].config.alarm_en = 1;
	TIMERG1.hw_timer[0].config.enable = 1;
}

//
// API

void gblcd_input_init()
{
	uint32_t tmp;
	gpio_config_t io_conf = {0};

	/// I2S (camera capture mode)

	frame = heap_caps_malloc(GBLCD_SIZE_DMA * 2, MALLOC_CAP_DMA);
	dma0 = (lldesc_t*)heap_caps_malloc(DMA_COUNT * sizeof(lldesc_t) * 2, MALLOC_CAP_DMA);
	dma1 = dma0 + DMA_COUNT;

	printf("[GB_in] frame %p dma %p\n", frame, dma0);

	// FB0
	tmp = GBLCD_SIZE_DMA;
	for(uint32_t i = 0; i < DMA_COUNT; i++)
	{
		dma0[i].size = tmp > MAX_DMA ? MAX_DMA : tmp;
		dma0[i].length = 0;
		dma0[i].sosf = 0;
		dma0[i].eof = 0;
		dma0[i].owner = 1;
		dma0[i].buf = frame + MAX_DMA * i;
		dma0[i].empty = (uint32_t)&dma0[i + 1];

		tmp -= MAX_DMA;
	}
	dma0[DMA_COUNT-1].empty = 0;

	// FB1
	tmp = GBLCD_SIZE_DMA;
	for(uint32_t i = 0; i < DMA_COUNT; i++)
	{
		dma1[i].size = tmp > MAX_DMA ? MAX_DMA : tmp;
		dma1[i].length = 0;
		dma1[i].sosf = 0;
		dma1[i].eof = 0;
		dma1[i].owner = 1;
		dma1[i].buf = frame + GBLCD_SIZE_DMA + MAX_DMA * i;
		dma1[i].empty = (uint32_t)&dma1[i + 1];

		tmp -= MAX_DMA;
	}
	dma1[DMA_COUNT-1].empty = 0;

	//
	periph_module_enable(PERIPH_I2S1_MODULE);
	periph_module_enable(PERIPH_TIMG1_MODULE);

	// init

	I2S1.conf.rx_reset = 1;
	I2S1.conf.rx_reset = 0;
	I2S1.conf.rx_fifo_reset = 1;
	I2S1.conf.rx_fifo_reset = 0;
	I2S1.lc_conf.in_rst = 1;
	I2S1.lc_conf.in_rst = 0;
	I2S1.lc_conf.ahbm_fifo_rst = 1;
	I2S1.lc_conf.ahbm_fifo_rst = 0;
	I2S1.lc_conf.ahbm_rst = 1;
	I2S1.lc_conf.ahbm_rst = 0;

	I2S1.conf.rx_slave_mod = 1;
	I2S1.conf.rx_right_first = 0;
	I2S1.conf.rx_msb_right = 0;
	I2S1.conf.rx_msb_shift = 0;
	I2S1.conf.rx_mono = 0;
	I2S1.conf.rx_short_sync = 0;

	I2S1.conf2.lcd_en = 1;
	I2S1.conf2.camera_en = 1;

	// configure clock divider
	I2S1.clkm_conf.clkm_div_a = 0;
	I2S1.clkm_conf.clkm_div_b = 0;
	I2S1.clkm_conf.clkm_div_num = 2;

	I2S1.fifo_conf.dscr_en = 1;
	I2S1.fifo_conf.rx_fifo_mod = 1;
	I2S1.fifo_conf.rx_fifo_mod_force_en = 1;

	I2S1.conf_chan.rx_chan_mod = 1;
	I2S1.sample_rate_conf.rx_bits_mod = 0;
	I2S1.timing.val = 0;
	I2S1.timing.rx_dsync_sw = 1;

	// config

	io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
	io_conf.pin_bit_mask = 1 << GBLCD_INPUT_S;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pull_up_en = 1;
	io_conf.pull_down_en = 0;
	gpio_config(&io_conf);
	gpio_install_isr_service(ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM);
	gpio_isr_handler_add(GBLCD_INPUT_S, capture_vsync_isr, NULL);
	gpio_intr_disable(GBLCD_INPUT_S);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_INPUT_S], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_INPUT_S, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GBLCD_INPUT_S, GPIO_FLOATING);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_INPUT_CP_ST], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_INPUT_CP_ST, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GBLCD_INPUT_CP_ST, GPIO_FLOATING);
	gpio_matrix_in(GBLCD_INPUT_CP_ST, I2S1I_WS_IN_IDX, false);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_INPUT_LD0], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_INPUT_LD0, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GBLCD_INPUT_LD0, GPIO_FLOATING);
	gpio_matrix_in(GBLCD_INPUT_LD0, I2S1I_DATA_IN0_IDX, false);

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GBLCD_INPUT_LD1], PIN_FUNC_GPIO);
	gpio_set_direction(GBLCD_INPUT_LD1, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GBLCD_INPUT_LD1, GPIO_FLOATING);
	gpio_matrix_in(GBLCD_INPUT_LD1, I2S1I_DATA_IN1_IDX, false);

	gpio_matrix_in(0x38, I2S1I_H_ENABLE_IDX, false);
	gpio_matrix_in(0x38, I2S1I_H_SYNC_IDX, false);
	gpio_matrix_in(0x38, I2S1I_V_SYNC_IDX, false);

	gpio_matrix_in(0x30, I2S1I_DATA_IN2_IDX, false);
	gpio_matrix_in(0x30, I2S1I_DATA_IN3_IDX, false);
	gpio_matrix_in(0x30, I2S1I_DATA_IN4_IDX, false);
	gpio_matrix_in(0x30, I2S1I_DATA_IN5_IDX, false);
	gpio_matrix_in(0x30, I2S1I_DATA_IN6_IDX, false);
	gpio_matrix_in(0x30, I2S1I_DATA_IN7_IDX, false);

	// frame timer
	TIMERG1.int_ena.val = 0;
	TIMERG1.int_clr_timers.val = 0xFFFFFFFF;
	TIMERG1.hw_timer[0].config.autoreload = 0;
	TIMERG1.hw_timer[0].config.divider = 80;
	TIMERG1.hw_timer[0].config.enable = 0;
	TIMERG1.hw_timer[0].config.increase = 1;
	TIMERG1.hw_timer[0].config.alarm_en = 0;
	TIMERG1.hw_timer[0].config.level_int_en = 1;
	TIMERG1.hw_timer[0].config.edge_int_en = 0;
	TIMERG1.hw_timer[0].load_high = 0;
	TIMERG1.hw_timer[0].load_low = 0;
	TIMERG1.hw_timer[0].alarm_high = 0;
	TIMERG1.hw_timer[0].alarm_low = 16000;
	TIMERG1.int_ena.t0 = 1;

	esp_intr_alloc(ETS_TG1_T0_LEVEL_INTR_SOURCE, ESP_INTR_FLAG_IRAM, finish_frame_isr, NULL, NULL);

	// queue
	event_queue = xQueueCreate(1, sizeof(void*));

	// enable
	gpio_intr_enable(GBLCD_INPUT_S);
}

void *gblcd_wait_frame()
{
	static void *ptr;
	xQueueReceive(event_queue, (void*)&ptr, portMAX_DELAY);
	return ptr;
}

