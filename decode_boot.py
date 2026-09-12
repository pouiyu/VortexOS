import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'b3.ppm'
data = open(path, 'rb').read()
idx = 0
for i in range(4):
    idx = data.find(b'\n', idx) + 1
w, h = 720, 400
px = data[idx:]

def get(x, y):
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]

# Render grid rows as ASCII: sample each 8x16 cell at 4px steps (2 samples per cell)
# Detect foreground: white or cyan/bright text on black
for r in range(0, 22):
    print(f'== grid row {r} ==')
    for sub in range(0, 16, 4):
        y = r * 16 + sub
        line = ''
        for x in range(40, 640, 4):
            rp, gp, bp = get(x, y)
            lit = (rp > 140 or gp > 140 or bp > 140)
            line += '#' if lit else '.'
        print(f'{sub:02d} {line}')
    print()
