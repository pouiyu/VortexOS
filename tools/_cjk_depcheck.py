#!/usr/bin/env python3
import importlib
for mod in ("PIL", "fontTools", "pytesseract", "numpy"):
    try:
        m = importlib.import_module(mod)
        ver = getattr(m, "__version__", "?")
        print(f"{mod}=OK v{ver}")
    except Exception as e:
        print(f"{mod}=NO ({type(e).__name__})")
import os
p = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
print("font_exists=", os.path.exists(p), "size=", os.path.getsize(p) if os.path.exists(p) else 0)