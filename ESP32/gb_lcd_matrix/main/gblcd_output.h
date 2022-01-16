// kgsws' GameBoy LCD output

#define GBLCD_WIDTH	160
#define GBLCD_HEIGHT	144
#define GBLCD_BUFFER_SIZE	((GBLCD_WIDTH * GBLCD_HEIGHT) / 4)

#define GBLCD_OUT_CPG	13
#define GBLCD_OUT_CPL	12
#define GBLCD_OUT_ST	14
#define GBLCD_OUT_LD0	27
#define GBLCD_OUT_LD1	26
#define GBLCD_OUT_CP	25
#define GBLCD_OUT_FR	18
#define GBLCD_OUT_S	19

uint8_t *gblcd_output_init();
uint8_t *gblcd_swap_buffers();
void gblcd_wait_vblank();

