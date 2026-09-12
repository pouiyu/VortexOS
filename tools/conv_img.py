from PIL import Image
import sys
src = sys.argv[1]
dst = sys.argv[2]
im = Image.open(src)
print('size=', im.size)
if im.size[0] > 800:
    r = 800.0 / im.size[0]
    im = im.resize((int(im.size[0]*r), int(im.size[1]*r)), Image.LANCZOS)
im.convert('RGB').save(dst, 'JPEG', quality=80)
print('saved', dst, im.size)