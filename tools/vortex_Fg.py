from PIL import Image, ImageDraw
import math

W, H = 64, 64
img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

cx, cy = W / 2, H / 2
a = 3.0
b = 0.09
max_theta = 20.5

for i in range(0, int(max_theta * 200)):
    t = i / 200.0
    r = a * math.exp(b * t)
    x = r * math.cos(t)
    y = r * math.sin(t)

    # 臂1
    px = cx + x
    py = cy + y
    if 0 <= px < W and 0 <= py < H:
        draw.ellipse([px-1, py-1, px+1, py+1], fill=(0,0,139))

    # 臂2
    px2 = cx - x
    py2 = cy - y
    if 0 <= px2 < W and 0 <= py2 < H:
        draw.ellipse([px2-1, py2-1, px2+1, py2+1], fill=(0,0,205))

# 中心亮点
draw.ellipse([cx-4, cy-4, cx+4, cy+4], fill=(0,0,255))

img.save('vortexFg.png')
print("Saved vortexFg.png")