
#define GBLCD_WIDTH	160
#define GBLCD_HEIGHT	144
#define GBLCD_SIZE	(GBLCD_WIDTH * GBLCD_HEIGHT)

#define GBLCD_INPUT_S	5	// vsync
#define GBLCD_INPUT_CP_ST	19	// clock | hsync
#define GBLCD_INPUT_LD0	21
#define GBLCD_INPUT_LD1	22

void gblcd_input_init();
void *gblcd_wait_frame();

