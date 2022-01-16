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
uint8_t fb[GBLCD_BUFSIZE];

uint32_t last_frame;

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

			memcpy(fb + (fp.frame & 3) * (GBLCD_BUFSIZE / 4), fp.data, GBLCD_BUFSIZE / 4);

			frame = fp.frame >> 2;

			if(last_frame != frame)
			{
				last_frame = frame;
				gblcd_update(fb);
			}
		}
	}

	return 0;
}

