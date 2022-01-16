// kgsws' GameBoy LCD output

#define GBLCD_WIDTH	160
#define GBLCD_HEIGHT	144
#define GBLCD_SIZE	(GBLCD_WIDTH * GBLCD_HEIGHT)

#define GBLCD_LINE_WIDTH	432
#define GBLCD_LINE_START	80
#define GBLCD_FRAME_HEIGHT	154

#define GBLCD_OUT_CPG	13
#define GBLCD_OUT_CPL	12
#define GBLCD_OUT_ST	14
#define GBLCD_OUT_LD0	27
#define GBLCD_OUT_LD1	26
#define GBLCD_OUT_CP	25
#define GBLCD_OUT_FR	18
#define GBLCD_OUT_S	19

void gblcd_output_init(void *framebuffer);

