import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_cjk_gui as g

root = g.tk.Tk()
app = g.CjkFontGui(root)

# 用 WSL 的字体作真实源测试光栅化
app.fontPathVar.set(r"\\wsl$\Ubuntu\usr\share\fonts\opentype\noto\NotoSansCJK-Regular.ttc")
app.renderVar.set("112"); app.binarizeVar.set("96"); app.thresholdVar.set("120")
glyph = app.rasterize("中", 112, 96, 120)
print("RENDER Result:", "OK" if glyph else "FAIL(font not loadable via UNC)")
if glyph:
    top = bottom = None
    for r in range(16):
        if any(glyph[r]):
            top = top if top is not None else r
            bottom = r
    print("content rows %s..%s" % (top, bottom))

def done():
    root.destroy()
    print("GUI SMOKE DONE")

root.after(400, done)
root.mainloop()