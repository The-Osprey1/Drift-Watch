"""Builds contact sheets from the PPM frames the host renderer writes.

Usage: python3 sheet.py OUT.png SCALE COLUMNS frame1.ppm [frame2.ppm ...]
SCALE 1 = native 240 x 240; 2 or 3 = enlarged with hard pixel edges for inspection.
A faint circle shows the physical round edge of the screen.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

out, scale, cols, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
size = 240 * scale
pad, label_h = 16, 22
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (pad + cols * (size + pad), pad + rows * (size + label_h + pad)), (24, 24, 28))
draw = ImageDraw.Draw(sheet)
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
for i, path in enumerate(files):
    im = Image.open(path).convert("RGB").resize((size, size), Image.NEAREST)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, size - 1, size - 1], fill=255)
    x = pad + (i % cols) * (size + pad)
    y = pad + (i // cols) * (size + label_h + pad)
    sheet.paste((48, 48, 54), (x, y, x + size, y + size))
    sheet.paste(im, (x, y), mask)
    draw.text((x, y + size + 4), os.path.basename(path)[:-4], font=font, fill=(200, 200, 205))
sheet.save(out)
