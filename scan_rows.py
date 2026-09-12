import sys

path = sys.argv[1] if len(sys.argv) > 1 else 't1.ppm'
data = open(path, 'rb').read()
idx = 0
for i in range(4):
    idx = data.find(b'\n', idx) + 1
w, h = 720, 400
px = data[idx:]

def get(x, y):
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]

# scan which grid rows contain white pixels
for r in range(25):
    cnt = 0
    for sub in range(0, 16, 2):
        for x in range(40, 640, 4):
            rp, gp, bp = get(x, r * 16 + sub)
            if rp > 180 and gp > 180 and bp > 180:
                cnt += 1
    if cnt > 10:
        print(f'row {r}: white samples={cnt}')
