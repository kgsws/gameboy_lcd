#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define UPD_PORT	2992

#define LCD_WIDTH	160
#define LCD_HEIGHT	144
#define LCD_SIZE	(LCD_WIDTH * LCD_HEIGHT)
//

typedef struct
{
	uint32_t frame;
	uint8_t data[1440];
} frame_packet_t;

//

int stopped;

int sck;
frame_packet_t fp[4];

uint32_t frame_count;
uint32_t capture_width = LCD_WIDTH * 3;
uint32_t capture_height = LCD_HEIGHT * 3;
uint32_t offset_x;
uint32_t offset_y;

static Display *display;
static Window root_win;

struct sockaddr_in dest_addr =
{
	.sin_family = AF_INET,
	.sin_port = ((UPD_PORT >> 8) | (UPD_PORT << 8)) & 0xFFFF
};

//
// funcs

static uint8_t rgb_to_shade(uint32_t input)
{
	int ret;
	float r, g, b;

	b = (float)(input & 0xFF) / 255.0f;
	g = (float)((input >> 8) & 0xFF) / 255.0f;
	r = (float)((input >> 16) & 0xFF) / 255.0f;

	ret = ((r * 0.299f + g * 0.587f + b * 0.114f) * 3.0f + /*0.5f*/ 0.75f);
	return 3 - ret;
}

static void convert_image(uint32_t *src)
{
	uint8_t *dst;

	for(uint32_t y = 0; y < LCD_HEIGHT; y++)
	{
		switch(y)
		{
			case (LCD_HEIGHT / 4) * 0:
				dst = fp[0].data;
			break;
			case (LCD_HEIGHT / 4) * 1:
				dst = fp[1].data;
			break;
			case (LCD_HEIGHT / 4) * 2:
				dst = fp[2].data;
			break;
			case (LCD_HEIGHT / 4) * 3:
				dst = fp[3].data;
			break;
		}
		for(uint32_t x = 0; x < LCD_WIDTH / 4; x++)
		{
			register uint8_t tmp;

			tmp = rgb_to_shade(*src) << 0;
			src++;
			tmp |= rgb_to_shade(*src) << 2;
			src++;
			tmp |= rgb_to_shade(*src) << 4;
			src++;
			tmp |= rgb_to_shade(*src) << 6;
			src++;

			*dst++ = tmp;
		}
		src += capture_width - LCD_WIDTH;
	}
}

static int init_capture()
{
	XWindowAttributes attributes;

	display = XOpenDisplay(NULL);
	if(!display)
		return 1;

	root_win = DefaultRootWindow(display);
	XGetWindowAttributes(display, root_win, &attributes);

	printf("XWindow: %u x %u\n", attributes.width, attributes.height);
	if(attributes.width < capture_width || attributes.height < capture_height)
		return 1;

	offset_x = (attributes.width / 2) - (capture_width / 2);
	offset_y = (attributes.height / 2) - (capture_height / 2);

	return 0;
}

//
// MAIN

int main(int argc, void **argv)
{
	if(argc < 2)
	{
		printf("usage: %s IP\n", argv[0]);
		return 1;
	}

	if(init_capture())
	{
		printf("XWindow capture init failed\n");
		return 1;
	}

	sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sck < 0)
	{
		printf("socket creation failed\n");
		return 1;
	}

	dest_addr.sin_addr.s_addr = inet_addr(argv[1]);
	if(connect(sck, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0)
	{
		printf("connect failed %d\n", errno);
		return 1;
	}

	while(!stopped)
	{
		XImage *img;
		uint32_t *ptr;
		uint32_t idx;

		// CAPTURE
		img = XGetImage(display, root_win, offset_x, offset_y, capture_width, capture_height, AllPlanes, ZPixmap);

		// convert & send
		idx = 0;
		ptr = (uint32_t*)img->data;
		for(uint32_t y = 0; y < 3; y++)
		{
			for(uint32_t x = 0; x < 3; x++)
			{
				// convert
				convert_image(ptr);
				// send
				for(uint32_t i = 0; i < 4; i++)
				{
					fp[i].frame = (frame_count << 6) | (idx << 2) | i;
					send(sck, fp + i, sizeof(frame_packet_t), 0);
				}
				// next
				idx++;
				ptr += LCD_WIDTH;
			}
			ptr += LCD_WIDTH * (LCD_HEIGHT - 1) * 3;
		}

		XDestroyImage(img);

		frame_count++;
		usleep(33 * 1000);
	}

	XCloseDisplay(display);

	return 0;
}

