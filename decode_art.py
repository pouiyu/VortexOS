import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'cap4.ppm'
data = open(path, 'rb').read()
idx = 0
for i in range(4):
    idx = data.find(b'\n', idx) + 1
w, h = 720, 400
px = data[idx:]

def get(x, y):
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]

r0 = int(sys.argv[2]) if len(sys.argv) > 2 else 7
r1 = int(sys.argv[3]) if len(sys.argv) > 3 else 14
for r in range(r0, r1):
    print(f'== grid row {r} ==')
    for sub in range(0, 16, 2):
        y = r * 16 + sub
        line = ''
        for x in range(40, 640, 4):
            rp, gp, bp = get(x, y)
            lit = (rp > 180 and gp > 180 and bp > 180)
            line += '#' if lit else '.'
        print(f'{sub:02d} {line}')
    print()
