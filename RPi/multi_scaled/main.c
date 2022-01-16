#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "gblcd.h"

#define UPD_PORT	2991

typedef struct
{
	uint32_t frame;
	uint8_t data[1440]; // 1/4 of full frame
} frame_packet_t;

struct sockaddr_in in_addr =
{
	.sin_family = AF_INET,
	.sin_port = ((UPD_PORT >> 8) | (UPD_PORT << 8)) & 0xFFFF
};

//

int sck;
struct pollfd pfd[1];

frame_packet_t fp;
uint8_t inputfb[GBLCD_WIDTH * GBLCD_HEIGHT];
uint8_t fakefb[GBLCD_WIDTH * GBLCD_HEIGHT * GBLCD_SCREEN_COUNT];
uint8_t fb[GBLCD_BUFSIZE * GBLCD_SCREEN_COUNT];

uint32_t last_frame;

//
//

static void save_debug(const char *name, void *ptr, uint32_t size)
{
	int fd;

	fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fd < 0)
		return;

	write(fd, ptr, size);

	close(fd);
}

static void convert_screen(uint8_t *dst, uint8_t *src, uint32_t stride)
{
	stride -= GBLCD_WIDTH;
	for(uint32_t y = 0; y < GBLCD_HEIGHT; y++)
	{
		for(uint32_t x = 0; x < GBLCD_WIDTH / 4; x++)
		{
			register uint8_t tmp;

			tmp = *src << 0;
			src++;
			tmp |= *src << 2;
			src++;
			tmp |= *src << 4;
			src++;
			tmp |= *src << 6;
			src++;

			*dst++ = tmp;
		}
		src += stride;
	}
}

static void scale_conversion()
{
	uint8_t *src;
	uint8_t *dst;
	uint32_t idx;

	// convert 2bpp to 8bpp; in place
	src = inputfb + GBLCD_BUFSIZE;
	dst = inputfb + sizeof(inputfb);
	for(uint32_t i = 0; i < GBLCD_BUFSIZE; i++)
	{
		register uint8_t pixels;

		src--;
		pixels = *src;

		dst--;
		*dst = (pixels >> 6) & 3;
		dst--;
		*dst = (pixels >> 4) & 3;
		dst--;
		*dst = (pixels >> 2) & 3;
		dst--;
		*dst = (pixels >> 0) & 3;
	}

	// scale 3x
	src = inputfb;
	dst = fakefb;
	for(uint32_t y = 0; y < GBLCD_HEIGHT; y++)
	{
		for(uint8_t i = 0; i < 3; i++)
		{
			for(uint32_t x = 0; x < GBLCD_WIDTH; x++)
			{
				register uint8_t in = *src++;
				*dst++ = in;
				*dst++ = in;
				*dst++ = in;
			}
			src -= GBLCD_WIDTH;
		}
		src += GBLCD_WIDTH;
	}

	// convert
	src = fakefb;
	dst = fb;
	for(uint32_t y = 0; y < 3; y++)
	{
		for(uint32_t x = 0; x < 3; x++)
		{
			convert_screen(dst, src, GBLCD_WIDTH * 3);
			src += GBLCD_WIDTH;
			dst += GBLCD_BUFSIZE;
		}
		src += GBLCD_WIDTH * (GBLCD_HEIGHT - 1) * 3;
	}
}

//
//

int main(int argc, void **argv)
{
	printf("-= kgsws' GameBoy LCD output =-\n");

	sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sck < 0)
	{
		printf("- socket creation failed\n");
		return 1;
	}

	if(bind(sck, (struct sockaddr*)&in_addr, sizeof(in_addr)) < 0)
	{
		printf("- bind failed %d\n", errno);
		return 1;
	}

	if(gblcd_init("/dev/fb0"))
	{
		printf("- failed init framebuffer\n");
		return 1;
	}

	pfd[0].events = POLLIN;
	pfd[0].fd = sck;

	while(1)
	{
		poll(pfd, 1, 50);

		if(pfd[0].revents & POLLIN)
		{
			uint32_t frame;

			recv(sck, (void*)&fp, sizeof(frame_packet_t), 0);

			memcpy(inputfb + (fp.frame & 3) * (GBLCD_BUFSIZE / 4), fp.data, GBLCD_BUFSIZE / 4);

			frame = fp.frame >> 2;

			if(last_frame != frame)
			{
				// scale the image
				scale_conversion();
				// update screen
				last_frame = frame;
				gblcd_update(fb);
			}
		}
	}

	return 0;
}

