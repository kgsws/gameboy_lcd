# Gameboy LCD stuff

This repository contains applications used in my Gameboy LCD video.

## Pin naming

LCD pin naming used in this repository matches the Gameboy CPU pinout.

## ESP32

Every application has to be compiled using cmake method:
```
mkdir build
cd build
cmake ..
make
```

List of applications:

- `gb_lcd_capture` capture and stream image to PC
- `gb_lcd_out` generate LCD timing and receive image from network
  - this contains more memory intensive singnal generation code
  - this generates control signals more closer to the original
- `gb_lcd_matrix` stand-alone application, provided as a demo for LCD API
  - this contains less memory intensive singal generation code
  - pixel clock is halved, without lowering the frame rate

Use the file `wifi_info.h` for network configuration.

## PC

Simple makefile.

List of applications:

- `recv` receive and show image stream from the network
- `send` capture your screen (Linux X windows only) and send over the network
- `send_3x` same as above, but for 9 LCDs

## RPi

Every example here generates modified control signals. That is 2 MHz pixel clock version.

- `solo` generate LCD timing and receive image from network
- `multi_scaled` same as above, but received image is scaled 3x, for 9 LCDs
- `multi_screen` almost as above, but each pixel is controled individually
- `clock` stand-alone application, provided as a demo for LCD API

### Important RPi stuff

The code fills framebuffer with generated video waveforms. It expects RGB DPI mode to be active.
However, you can't just run any application, as there is no FPGA involved.
If you use something that uses framebuffer the standard way, it will generate garbage signals for LCDs.
This might even damage them. Every application has to be modified to generate correct signals.

### Pin mapping

This is a default configuration and i relies on BGRA framebuffer setting. If you happen to use RGBA just swap RED with BLUE.
LCD Data are always on two consecutive pins.
In single-screen code only first to bits of GREEN channel are used.

- `B0` CPG
- `B1` CPL
- `B2` ST
- `B3` CP
- `B4` FR
- `B5` S
- `B6` LD0
- `B7` LD1

- `G0` LD0 (used in single screen mode)
- `G1` LD1 (used in single screen mode)
- `G2` LD0
- `G3` LD1
- `G4` LD0
- `G5` LD1
- `G6` LD0
- `G7` LD1

- `R0` LD0
- `R1` LD1
- `R2` LD0
- `R3` LD1
- `R4` LD0
- `R5` LD1
- `R6` LD0
- `R7` LD1

### config.txt

Use FKMS mode! Example code requires framebuffer to exist. Use `legacy` version of raspberry Pi OS.

Add this at the end of the file:
```
dtoverlay=dpi24
dpi_output_format=0x76017
dpi_timings=408 0 0 6 0 307 0 0 1 0 0 0 0 60 0 3840000 1
dpi_group=2
dpi_mode=87
framebuffer_width=408
framebuffer_height=307
enable_dpi_lcd=1
display_default_lcd=1
```

### cmdline.txt

Add this to the first line `fbcon=map:2`. Remember, this file should contain only one line.

This is important as it disables text cosole. Text console would mess up generated signals.
