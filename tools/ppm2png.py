from PIL import Image
im = Image.open('/tmp/fb_shot.ppm').convert('RGB')
im.save('/tmp/fb_shot.png')
print('saved', im.size)