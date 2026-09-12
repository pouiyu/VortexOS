#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ui_designer.py —— VortexOS 可视化 UI 设计器(Python/Tkinter 重写)

拖动 6 种组件到客户区画布, 右侧改属性, 一键生成可编译的 C 程序文件,
并可一键用 Makefile 的 `make program` 目标(经 WSL)编译成 ELF。

生成的代码构建在 libwidget 运行时库之上(src/include/gui/libwidget.h),
字段顺序: WD_*, x, y, w, h, "text", fg, bg, border, down, buf, cap, len, checked。

用法:  python tools/ui_designer.py
"""

import os
import re
import subprocess
import tempfile
import tkinter as tk
from tkinter import ttk, colorchooser, messagebox, filedialog

# ---- 组件模型 ----
TYPE_NAMES = ['Button', 'Label', 'TextBox', 'CheckBox', 'Line', 'Rect']
WD_TYPES   = ['WD_BUTTON', 'WD_LABEL', 'WD_TEXTBOX', 'WD_CHECKBOX', 'WD_LINE', 'WD_RECT']

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
TRANS = (0, 0, 0)  # 透明色在生成时用黑色替代(不影响绘制)

# 每种组件的默认尺寸与颜色
DEFAULTS = {
    'Button':   dict(w=120, h=36, fg=WHITE,  bg=(60, 110, 200), border=(20, 40, 90), text='Button'),
    'Label':    dict(w=120, h=20, fg=(40, 40, 40), bg=TRANS, border=TRANS, text='Label'),
    'TextBox':  dict(w=180, h=28, fg=(20, 20, 20), bg=WHITE, border=(120, 120, 120), text='text', cap=32),
    'CheckBox': dict(w=140, h=22, fg=(40, 40, 40), bg=WHITE, border=(120, 120, 120), text='Check', checked=True),
    'Line':     dict(w=200, h=2,  fg=BLACK, bg=BLACK, border=(180, 180, 180), text=''),
    'Rect':     dict(w=200, h=60, fg=BLACK, bg=BLACK, border=(140, 140, 140), text=''),
}


def _clr(c):
    """(r,g,b) -> '#rrggbb'"""
    return '#%02X%02X%02X' % tuple(c)


def _rgb(c):
    """(r,g,b) -> 'guiRgb(r,g,b)'"""
    return 'guiRgb(%d,%d,%d)' % tuple(c)


def _esc(s):
    """C 字符串转义"""
    return s.replace('\\', '\\\\').replace('"', '\\"')


def project_root():
    """返回项目根(本文件位于 <root>/tools)"""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def wsl_root():
    """项目根的 WSL(/mnt/<d>/...) 路径"""
    r = project_root()
    drive = r[0].lower()
    rest = r[2:].replace('\\', '/')
    return '/mnt/%s%s' % (drive, rest)


class UiDesigner:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title('VortexOS UI 设计器 · 拖动组件生成 C 程序 (Python)')
        self.root.geometry('1120x780')
        self.root.minsize(920, 640)

        self.items = []          # list[dict]
        self.sel = -1
        self.next_id = {t: 1 for t in TYPE_NAMES}
        self.drag = None         # None / ('move', dx, dy) / ('resize',)
        self.win_w, self.win_h = 360, 240
        self.body_bg = (240, 240, 240)
        self.title_color = (60, 80, 170)
        self.win_title = 'My UI'

        # 属性面板变量
        self.vars = {}
        self._loading = False

        self._build_top()
        self._build_left()
        self._build_canvas()
        self._build_right()
        self._sync_props()

    # ---------------- 顶部 ----------------
    def _build_top(self):
        bar = ttk.Frame(self.root, padding=(8, 6))
        bar.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(bar, text='窗口标题').pack(side=tk.LEFT)
        self.v_title = tk.StringVar(value=self.win_title)
        # 输入即时同步到 self.win_title, 否则生成时永远用默认标题
        self.v_title.trace_add('write', lambda *a: setattr(self, 'win_title', self.v_title.get()))
        ttk.Entry(bar, textvariable=self.v_title, width=16).pack(side=tk.LEFT, padx=(6, 14))
        ttk.Label(bar, text='宽').pack(side=tk.LEFT)
        self.v_w = tk.IntVar(value=self.win_w)
        ttk.Spinbox(bar, from_=100, to=2048, textvariable=self.v_w, width=6,
                    command=self._apply_win_size).pack(side=tk.LEFT, padx=(4, 10))
        ttk.Label(bar, text='高').pack(side=tk.LEFT)
        self.v_h = tk.IntVar(value=self.win_h)
        ttk.Spinbox(bar, from_=100, to=2048, textvariable=self.v_h, width=6,
                    command=self._apply_win_size).pack(side=tk.LEFT, padx=(4, 14))
        ttk.Button(bar, text='客户区底色', command=lambda: self._pick_color('body_bg')).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text='标题栏色', command=lambda: self._pick_color('title_color')).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text='清空', command=self._clear_all).pack(side=tk.LEFT, padx=2)
        ttk.Button(bar, text='生成 C 程序…', command=self._generate).pack(side=tk.RIGHT)
        ttk.Button(bar, text='编译 (make)', command=self._compile).pack(side=tk.RIGHT, padx=(0, 8))

    def _apply_win_size(self):
        self.win_w = max(50, self.v_w.get())
        self.win_h = max(50, self.v_h.get())
        self._redraw()

    # ---------------- 左侧: 添加组件 + 列表 ----------------
    def _build_left(self):
        left = ttk.Frame(self.root, padding=8)
        left.pack(side=tk.LEFT, fill=tk.Y)
        ttk.Label(left, text='添加组件').pack(anchor=tk.W)
        for t in TYPE_NAMES:
            ttk.Button(left, text='添加 %s' % t, width=16,
                       command=lambda tt=t: self.add_item(tt)).pack(fill=tk.X, pady=1)
        ttk.Label(left, text='组件列表', padding=(0, 8, 0, 2)).pack(anchor=tk.W)
        self.listbox = tk.Listbox(left, width=20, height=16)
        self.listbox.pack(fill=tk.Y, expand=True)
        self.listbox.bind('<<ListboxSelect>>', self._on_list_select)
        ttk.Button(left, text='删除选中 (Del)', command=self._delete_selected).pack(fill=tk.X, pady=2)

    def _on_list_select(self, _evt=None):
        sel = self.listbox.curselection()
        if sel:
            self.sel = sel[0]
            self._sync_props()
            self._redraw()

    def _delete_selected(self):
        if 0 <= self.sel < len(self.items):
            self.items.pop(self.sel)
            self.sel = -1
            self._sync_list()
            self._sync_props()
            self._redraw()

    def _clear_all(self):
        self.items.clear()
        self.sel = -1
        self._sync_list()
        self._sync_props()
        self._redraw()

    # ---------------- 画布 ----------------
    def _build_canvas(self):
        frame = ttk.Frame(self.root)
        frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.canvas = tk.Canvas(frame, bg='#C8C8C8', highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind('<Button-1>', self._on_mouse_down)
        self.canvas.bind('<B1-Motion>', self._on_mouse_move)
        self.canvas.bind('<ButtonRelease-1>', self._on_mouse_up)
        self.canvas.bind('<Button-3>', self._on_right_click)

    def _hit(self, x, y):
        for k in range(len(self.items) - 1, -1, -1):
            it = self.items[k]
            if it['x'] <= x < it['x'] + it['w'] and it['y'] <= y < it['y'] + it['h']:
                return k
        return -1

    def _on_mouse_down(self, ev):
        self.canvas.focus_set()
        k = self._hit(ev.x, ev.y)
        if k >= 0:
            self.sel = k
            it = self.items[k]
            if ev.x >= it['x'] + it['w'] - 8 and ev.y >= it['y'] + it['h'] - 8:
                self.drag = ('resize',)
            else:
                self.drag = ('move', ev.x - it['x'], ev.y - it['y'])
        else:
            self.sel = -1
            self.drag = None
        self._sync_list()
        self._sync_props()
        self._redraw()

    def _on_mouse_move(self, ev):
        if not self.drag or self.sel < 0:
            return
        it = self.items[self.sel]
        mode = self.drag[0]
        if mode == 'move':
            _, dx, dy = self.drag
            it['x'] = max(0, ev.x - dx)
            it['y'] = max(0, ev.y - dy)
        else:
            it['w'] = max(20, ev.x - it['x'])
            it['h'] = max(10, ev.y - it['y'])
        self._redraw()

    def _on_mouse_up(self, _ev):
        self.drag = None

    def _on_right_click(self, ev):
        k = self._hit(ev.x, ev.y)
        if k >= 0:
            self.items.pop(k)
            if self.sel == k:
                self.sel = -1
            self._sync_list()
            self._sync_props()
            self._redraw()

    def _draw_item(self, it):
        x, y, w, h = it['x'], it['y'], it['w'], it['h']
        c = self.canvas
        t = it['type']
        if t == 0:  # Button
            c.create_rectangle(x, y, x + w, y + h, fill=_clr(it['bg']), outline=_clr(it['border']))
            tw = self._text_w(it['text'])
            c.create_text(x + (w - tw) / 2, y + h / 2, text=it['text'], anchor='w',
                          fill=_clr(it['fg']), font=self._font())
        elif t == 1:  # Label
            c.create_text(x, y, text=it['text'], anchor='nw', fill=_clr(it['fg']), font=self._font())
        elif t == 2:  # TextBox
            c.create_rectangle(x, y, x + w, y + h, fill=_clr(it['bg']), outline=_clr(it['border']))
            c.create_text(x + 2, y + 2, text=it['text'], anchor='nw', fill=_clr(it['fg']), font=self._font())
        elif t == 3:  # CheckBox
            c.create_rectangle(x, y, x + 14, y + 14, fill=_clr(it['bg']), outline=_clr(it['border']))
            if it.get('checked'):
                c.create_line(x + 2, y + 8, x + 5, y + 11, fill=_clr(it['fg']))
                c.create_line(x + 5, y + 11, x + 12, y + 3, fill=_clr(it['fg']))
            c.create_text(x + 20, y, text=it['text'], anchor='nw', fill=_clr(it['fg']), font=self._font())
        elif t == 4:  # Line
            c.create_line(x, y + h / 2, x + w, y + h / 2, fill=_clr(it['border']))
        elif t == 5:  # Rect
            c.create_rectangle(x, y, x + w, y + h, outline=_clr(it['border']))

    def _font(self):
        try:
            return ('Microsoft YaHei', 9)
        except Exception:
            return ('TkDefaultFont', 9)

    def _text_w(self, s):
        try:
            return int(self.canvas.textwidth(s, font=self._font()))
        except Exception:
            return len(s) * 10

    def _redraw(self):
        c = self.canvas
        c.delete('all')
        # 客户区(所见即所得, 超出裁掉)
        bw, bh = self.win_w, self.win_h
        c.create_rectangle(0, 0, max(1, bw), max(1, bh), fill=_clr(self.body_bg), outline='')
        for idx, it in enumerate(self.items):
            self._draw_item(it)
            if idx == self.sel:
                x, y, w, h = it['x'], it['y'], it['w'], it['h']
                c.create_rectangle(x, y, x + w, y + h, outline='#008CFF', width=2)
                c.create_rectangle(x + w - 6, y + h - 6, x + w, y + h, fill='#008CFF', outline='')

    # ---------------- 右侧: 属性面板 ----------------
    def _build_right(self):
        panel = ttk.Frame(self.root, padding=8)
        panel.pack(side=tk.RIGHT, fill=tk.Y, padx=(0, 6))
        row = [0]

        def add_row(label, key, spin=None):
            r = row[0]; row[0] += 1
            ttk.Label(panel, text=label).grid(row=r, column=0, sticky='w', pady=1)
            if spin:
                var = tk.IntVar()
                w = ttk.Spinbox(panel, from_=spin[0], to=spin[1], textvariable=var, width=9,
                                command=lambda kk=key, vv=var: self._prop_int(kk, vv))
            else:
                var = tk.StringVar()
                w = ttk.Entry(panel, textvariable=var, width=10)
            w.grid(row=r, column=1, sticky='ew', pady=1, columnspan=2)
            self.vars[key] = (var, w, spin is not None)

        add_row('Name', 'name')
        add_row('Text', 'text')
        add_row('X', 'x', (0, 4095))
        add_row('Y', 'y', (0, 4095))
        add_row('W', 'w', (10, 4095))
        add_row('H', 'h', (10, 4095))
        add_row('Cap', 'cap', (2, 4095))

        def make_color_row(label, key):
            r = row[0]; row[0] += 1
            ttk.Label(panel, text=label).grid(row=r, column=0, sticky='w', pady=1)
            var = tk.StringVar()
            entry = ttk.Entry(panel, textvariable=var, width=8)
            entry.grid(row=r, column=1, sticky='ew', pady=1)
            entry.bind('<Return>', lambda e, kk=key, vv=var: self._prop_hex(kk, vv.get()))
            ttk.Button(panel, text='…', width=3,
                       command=lambda kk=key: self._pick_prop_color(kk)).grid(
                row=r, column=2, padx=(2, 0))
            self.vars[key] = (var, entry, False)

        make_color_row('Fg', 'fg')
        make_color_row('Bg', 'bg')
        make_color_row('Border', 'border')

        # Checked(仅 CheckBox)
        self.v_check = tk.BooleanVar()
        self.chk = ttk.Checkbutton(panel, text='Checked', variable=self.v_check,
                                   command=self._prop_checked)
        self.chk.grid(row=row[0], column=0, columnspan=3, sticky='w', pady=4)

        # 文本回车提交
        for key in ('name', 'text'):
            self.vars[key][1].bind('<Return>', lambda e, kk=key: self._prop_str(kk, self.vars[kk][0].get()))
            self.vars[key][1].bind('<FocusOut>', lambda e, kk=key: self._prop_str(kk, self.vars[kk][0].get()))

    def _prop_int(self, key, var):
        if self._loading or self.sel < 0:
            return
        try:
            self.items[self.sel][key] = int(var.get())
        except Exception:
            return
        self._redraw()

    def _prop_str(self, key, val):
        if self._loading or self.sel < 0:
            return
        self.items[self.sel][key] = val
        self._redraw()

    def _prop_hex(self, key, val):
        if self._loading or self.sel < 0:
            return
        m = re.fullmatch(r'#?([0-9a-fA-F]{6})', val.strip())
        if not m:
            return
        hx = m.group(1)
        self.items[self.sel][key] = (int(hx[0:2], 16), int(hx[2:4], 16), int(hx[4:6], 16))
        self._redraw()

    def _prop_checked(self):
        if self._loading or self.sel < 0:
            return
        self.items[self.sel]['checked'] = bool(self.v_check.get())
        self._redraw()

    def _pick_prop_color(self, key):
        if not (0 <= self.sel < len(self.items)):
            return
        self._pick_color_for(key, self.items[self.sel][key])

    def _pick_color(self, key):
        cur = self.body_bg if key == 'body_bg' else self.title_color
        if key == 'body_bg':
            self._pick_color_for(None, cur)
        else:
            self._pick_color_for(key, cur)

    def _pick_color_for(self, key, cur):
        c, ok = colorchooser.askcolor(_clr(cur), parent=self.root, title='选择颜色')
        if not ok:
            return
        if key is None:
            self.body_bg = c
        elif key == 'title_color':
            self.title_color = c
        elif key == 'body_bg':
            self.body_bg = c
        elif self.sel >= 0:
            self.items[self.sel][key] = c
        self._sync_props()
        self._redraw()

    def _sync_list(self):
        self.listbox.delete(0, tk.END)
        for it in self.items:
            self.listbox.insert(tk.END, '%s  (%s)' % (it['name'], it['typeName']))
        if 0 <= self.sel < len(self.items):
            self.listbox.selection_clear(0, tk.END)
            self.listbox.selection_set(self.sel)
            self.listbox.see(self.sel)

    def _sync_props(self):
        self._loading = True
        cur = self.items[self.sel] if 0 <= self.sel < len(self.items) else None
        for key in ('name', 'text', 'x', 'y', 'w', 'h', 'cap'):
            if key in self.vars and cur is not None:
                v = cur.get(key, 0)
                if key in ('x', 'y', 'w', 'h', 'cap'):
                    self.vars[key][0].set(v)
                else:
                    self.vars[key][0].set(v)
        for key in ('fg', 'bg', 'border'):
            if key in self.vars:
                self.vars[key][0].set(_clr(cur[key]) if cur else '')
        self.chk.state(['!selected', '!disabled'] if cur else ['disabled'])
        if cur:
            self.v_check.set(bool(cur.get('checked')))
        self._loading = False

    # ---------------- 添加组件 ----------------
    def add_item(self, t):
        d = DEFAULTS[t].copy()
        it = d
        it.update(name=t.lower() + str(self.next_id[t]), type=TYPE_NAMES.index(t),
                  typeName=t, x=20, y=20 + (len(self.items) % 8) * 22)
        if 'cap' not in it:
            it['cap'] = 0
        if 'checked' not in it:
            it['checked'] = False
        self.next_id[t] += 1
        self.items.append(it)
        self.sel = len(self.items) - 1
        self._sync_list()
        self._sync_props()
        self._redraw()

    # ---------------- 生成 C 源码 ----------------
    def generate(self):
        lines = []
        lines.append('// Generated by ui_designer.py — compilable VortexOS GUI program.')
        lines.append('#include <gui/libwidget.h>')
        for it in self.items:
            if it['type'] == 2:
                lines.append('static char %s_buf[%d];' % (it['name'], max(2, it['cap'])))
        lines.append('static Widget ws[] = {')
        for k, it in enumerate(self.items):
            fields = [WD_TYPES[it['type']], it['x'], it['y'], it['w'], it['h'],
                      '"%s"' % _esc(it['text']), _rgb(it['fg']), _rgb(it['bg']), _rgb(it['border']), 0]
            if it['type'] == 2:
                single, scap, chk = '%s_buf' % it['name'], max(2, it['cap']), 0
            else:
                single, scap, chk = 0, 0, (1 if it['type'] == 3 and it['checked'] else 0)
            fields += [single, scap, 0, chk]
            sep = ',' if k < len(self.items) - 1 else ''
            lines.append('    { %s }%s' % (', '.join(str(f) for f in fields), sep))
        lines.append('};')
        lines.append('#define N (sizeof(ws)/sizeof(ws[0]))')
        lines.append('void _start(void) {')
        lines.append('    int win = guiCreateWindow("%s", 120, 90, %d, %d, %s, %s, WM_STYLE_DEFAULT);' % (
            _esc(self.win_title), self.win_w, self.win_h, _rgb(self.title_color), _rgb(self.body_bg)))
        lines.append('    if (win < 0) guiExit();')
        lines.append('    wdInit(ws, N);')
        lines.append('    int focus = -1;')
        lines.append('    int prevDown = 0;')
        lines.append('    wdDraw(win, ws, N); guiFlush();')
        lines.append('    for (;;) {')
        lines.append('        WmEvent ev; guiPoll(&ev);')
        lines.append('        if (ev.key == KEY_ESC) break;')
        lines.append('        int wx, wy; guiGetWinPos(win, &wx, &wy);')
        lines.append('        int mx = ev.mouseX - wx;')
        lines.append('        int my = ev.mouseY - wy - WM_TITLEBAR_H;')
        lines.append('        int changed = 0;')
        lines.append('        int down = ev.leftDown;')
        lines.append('        if (!down && prevDown) {')
        lines.append('            /* 松开沿: 结束当前组件交互, 复位按钮按下态 */')
        lines.append('            if (focus >= 0) changed = wdPress(win, ws, N, focus, 0);')
        lines.append('        } else if (down && !prevDown) {')
        lines.append('            /* 按下沿: 命中并开始交互(复选在此切换, 文本框获焦) */')
        lines.append('            int idx = wdHit(ws, N, mx, my);')
        lines.append('            if (idx >= 0) { focus = idx; changed = wdPress(win, ws, N, idx, 1); }')
        lines.append('            else focus = -1;')
        lines.append('        }')
        lines.append('        prevDown = down;')
        lines.append('        changed |= wdHandleKey(win, ws, N, &focus, ev.key);')
        lines.append('        if (changed) { wdDraw(win, ws, N); guiFlush(); }')
        lines.append('    }')
        lines.append('    guiExit();')
        lines.append('}')
        return '\n'.join(lines) + '\n'

    def _program_name(self, name):
        return re.sub(r'[^0-9A-Za-z_]', '_', name) or 'myapp'

    def _generate(self):
        if not self.items:
            messagebox.showinfo('提示', '先添加至少一个组件')
            return
        name = self._program_name(self.win_title)
        path = filedialog.asksaveasfilename(
            title='保存生成的 C 程序', defaultextension='.c',
            initialdir=os.path.join(project_root(), 'src', 'user'),
            initialfile=name + '.c', filetypes=[('C 源码', '*.c')])
        if not path:
            return
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(self.generate())
        messagebox.showinfo('完成', '已生成: %s\n\n下一步用「编译 (make)」或 build 命令编译。' % path)

    def _compile(self):
        if not self.items:
            messagebox.showinfo('提示', '先添加至少一个组件')
            return
        name = self._program_name(self.win_title)
        path = os.path.join(project_root(), 'src', 'user', name + '.c')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(self.generate())
        cmd = ['wsl.exe', '--', 'bash', '-lc',
               'cd %s && make program NAME=%s' % (wsl_root(), name)]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
            out = (r.stdout or '') + (r.stderr or '')
        except Exception as e:
            messagebox.showerror('编译失败', '无法启动 WSL: %s' % e)
            return
        detail = '退出码 %d\n\n' % r.returncode + out.strip()
        if r.returncode == 0:
            messagebox.showinfo('完成', '编译成功 → system/programs/%s.elf\n\n%s' % (name, detail))
        else:
            messagebox.showerror('编译失败', detail)

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    UiDesigner().run()