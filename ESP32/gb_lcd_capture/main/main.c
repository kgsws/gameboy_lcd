#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "wifi_info.h"
#include "gblcd_input.h"

typedef struct
{
	uint32_t frame;
	uint8_t data[1440]; // 1/4 of full frame
} frame_packet_t;

static uint32_t frame_count;

volatile uint_fast8_t wifi_on;

//
// WiFi

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
	} else
	if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		wifi_on = 0;
		esp_wifi_connect();
	} else
	if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		wifi_on = 1;
	}
}

static void wifi_init_sta()
{
	tcpip_adapter_init();

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

	wifi_config_t wifi_config =
	{
		.sta =
		{
			.ssid = WIFI_INFO_SSID,
			.password = WIFI_INFO_PSK
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );
}

//
//

static void lcd_task(void *unused)
{
	static struct sockaddr_in dest_addr;
	static int sck;
	static frame_packet_t fp;

	sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(DATA_TARGET_PORT);
	dest_addr.sin_addr.s_addr = inet_addr(DATA_TARGET_IP);

	connect(sck, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

	gblcd_input_init();

	while(1)
	{
		void *ptr;

		ptr = gblcd_wait_frame();
		if(wifi_on)
		{
#if 1
			uint16_t *src = ptr;

			for(uint32_t ii = 0; ii < 4; ii++)
			{
				uint8_t *dst = fp.data;

				fp.frame = ii | (frame_count << 2);

				for(uint32_t i = 0; i < GBLCD_SIZE / (4*4); i++)
				{
					register uint8_t tmp;

					tmp = *src++ << 2;
					tmp |= *src++ << 0;
					tmp |= *src++ << 6;
					tmp |= *src++ << 4;

					*dst++ = tmp;
				}

				send(sck, (void*)&fp, sizeof(frame_packet_t), 0);
			}
#else
			send(sck, ptr, 1024, 0);
#endif
			frame_count++;
		}
	}
}

void app_main(void)
{
	esp_err_t ret;

	printf("-= kgsws' GameBoy LCD capture =-\n");

	ret = nvs_flash_init();
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	wifi_init_sta();

	xTaskCreate(&lcd_task, "lcd task", 2048, NULL, 5, NULL);
}

