#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define UPD_PORT	2991

#define LCD_WIDTH	160
#define LCD_HEIGHT	144
#define LCD_SIZE	(LCD_WIDTH * LCD_HEIGHT)

#define SCALE	4

//

typedef struct
{
	uint32_t frame;
	uint8_t data[1440]; // 1/4 of full frame
} frame_packet_t;

//

SDL_Window *sdl_win;
SDL_GLContext sdl_glContext;
int stopped;

int sck;
frame_packet_t fp;
uint8_t fb[LCD_SIZE];
GLuint texture;

uint32_t last_frame;
int32_t last_frame_report = -1;
uint32_t tick_report;

struct pollfd pfd[1];

struct sockaddr_in in_addr =
{
	.sin_family = AF_INET,
	.sin_port = ((UPD_PORT >> 8) | (UPD_PORT << 8)) & 0xFFFF
};

static const uint8_t table[] =
{
#if 1
	255,
	170,
	85,
	0
#else
	255,
	255,
	255,
	255
#endif
};

//
// funcs

uint32_t get_time_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000L + ts.tv_nsec / 1000000);
}

//
// SDL

static uint32_t glui_sdl_init(uint16_t width, uint16_t height, const char *title)
{
	if(SDL_Init(SDL_INIT_VIDEO) != 0)
		return 1;

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetSwapInterval(1);

	sdl_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL);
	sdl_glContext = SDL_GL_CreateContext(sdl_win);

	glShadeModel(GL_SMOOTH);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(0.0f);

	// points
	glEnable(GL_POINT_SMOOTH);
	glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

	// lines
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

	// hints
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	// blend
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	return 0;
}

static void input()
{
	SDL_Event event;

	while(SDL_PollEvent(&event))
	{
		switch(event.type)
		{
			case SDL_QUIT:
				stopped = 1;
			break;
		}
	}
}

//
// MAIN

int main(int argc, void **argv)
{
	sck = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sck < 0)
	{
		printf("socket creation failed\n");
		return 1;
	}

	if(bind(sck, (struct sockaddr*)&in_addr, sizeof(in_addr)) < 0)
	{
		printf("bind failed %d\n", errno);
		return 1;
	}

	if(glui_sdl_init(LCD_WIDTH * SCALE, LCD_HEIGHT * SCALE, "kgsws' GameBoy LCD capture"))
	{
		printf("SDL init failed\n");
		return 1;
	}

	glMatrixMode(GL_PROJECTION);
	glOrtho(0, 1.0f, 1.0f, 0, 1.0f, -1.0f);

	glGenTextures(1, &texture);

	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glEnable(GL_TEXTURE_2D);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	pfd[0].events = POLLIN;
	pfd[0].fd = sck;

	while(!stopped)
	{
		poll(pfd, 1, 50);

		input();

		if(pfd[0].revents & POLLIN)
		{
			uint32_t frame;
			uint8_t *dst;
			uint8_t *src;

			recv(sck, (void*)&fp, sizeof(frame_packet_t), 0);

			frame = fp.frame >> 2;

			if(last_frame != frame)
			{
				last_frame = frame;

				if(last_frame_report < 0)
					last_frame_report = frame;

				glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LCD_WIDTH, LCD_HEIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, fb);

				glBegin(GL_TRIANGLE_STRIP);
					glTexCoord2f(0.0f, 0.0f);
					glVertex2f(0, 0);
					glTexCoord2f(1.0f, 0.0f);
					glVertex2f(1.0f, 0);
					glTexCoord2f(0.0f, 1.0f);
					glVertex2f(0, 1.0f);
					glTexCoord2f(1.0f, 1.0f);
					glVertex2f(1.0f, 1.0f);
				glEnd();

				SDL_GL_SwapWindow(sdl_win);
			}

			dst = fb + (fp.frame & 3) * (LCD_SIZE / 4);
			src = fp.data;

			for(uint32_t i = 0; i < 1440; i++)
			{
				register uint8_t tmp = *src++;

				*dst++ = table[(tmp >> 0) & 3];
				*dst++ = table[(tmp >> 2) & 3];
				*dst++ = table[(tmp >> 4) & 3];
				*dst++ = table[(tmp >> 6) & 3];
			}
		}

		// framerate
		if(last_frame_report >= 0)
		{
			uint32_t ticks = get_time_ms();

			if(ticks - tick_report >= 1000)
			{
				float seconds = (float)(ticks - tick_report) * 0.001f;
				float frames = ((last_frame - last_frame_report) & 0xFFFFFF);

				printf("%f FPS; %f %f\n", frames / seconds, frames, seconds);

				last_frame_report = last_frame;

				tick_report = ticks;
			}
		}
	}

	return 0;
}

