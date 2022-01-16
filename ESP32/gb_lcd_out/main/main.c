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
#include "lwip/udp.h"

#include "wifi_info.h"
#include "gblcd_output.h"

typedef struct
{
	uint32_t frame;
	uint8_t data[1440]; // 1/4 of full frame
} frame_packet_t;

static uint8_t framebuffer[GBLCD_SIZE / 4];

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

static void udp_recv_func(void *arg, struct udp_pcb *upcb, struct pbuf *p, const struct ip_addr *addr, uint16_t port)
{
	frame_packet_t *fp = p->payload;
	memcpy(framebuffer + (fp->frame & 3) * (GBLCD_SIZE / 16), fp->data, 1440);
	pbuf_free(p);
}

static void lcd_task(void *unused)
{
	static struct udp_pcb *upcb;

	upcb = udp_new();
	udp_bind(upcb, IP_ADDR_ANY, DATA_TARGET_PORT);
	udp_recv(upcb, udp_recv_func, NULL);

	gblcd_output_init(framebuffer);

	while(1)
	{
		vTaskDelay(1000);
	}
}

void app_main(void)
{
	esp_err_t ret;

	printf("-= kgsws' GameBoy LCD control =-\n");

	ret = nvs_flash_init();
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	wifi_init_sta();
	esp_wifi_set_ps(WIFI_PS_NONE);

	xTaskCreatePinnedToCore(&lcd_task, "lcd task", 2048, NULL, 5, NULL, 1);
}

