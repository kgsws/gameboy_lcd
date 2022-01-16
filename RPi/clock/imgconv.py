#!/usr/bin/python3
from PIL import Image

image_list = [
	"0.png",
	"1.png",
	"2.png",
	"3.png",
	"4.png",
	"5.png",
	"6.png",
	"7.png",
	"8.png",
	"9.png",
	"mid0.png",
	"mid1.png",
	"leftover.png",
	"hi0.png",
	"hi1.png",
	"hi2.png"
]

##
def add_hexdump(source):
	global output
	i = 0
	for b in source:
		output.write("0x%02X," % b)
		i += 1
		if (i % 16) == 0:
			output.write("\n\t")

# image export
def export_image(name):
	global output
	img = Image.open(name)
	img.load()
	print("[IMAGE]", name, img.mode, img.width, "x", img.height)
	if img.width == 144 and img.height == 160:
		img = img.rotate(-90, expand=True)
	elif img.width != 160 or img.height != 144:
		raise ValueError("Invalid image resolution!")
	img = img.convert("L")
	img = img.tobytes()
	saved = bytearray()
	idx = 0
	for i in range(int(160 * 144 / 4)):
		outval = img[idx] >> 6
		idx += 1
		outval |= (img[idx] >> 4) & 12
		idx += 1
		outval |= (img[idx] >> 2) & 48
		idx += 1
		outval |= img[idx] & 192
		idx += 1
		saved.append(outval ^ 0xFF)
	add_hexdump(saved)

##
output = open("images.h", "w")

output.write("#define IMAGE_SIZE	5760\n")
output.write("static const uint8_t image_data[] =\n{\n\t")
for iname in image_list:
	export_image(iname)
output.write("\n};\n")

