# ============================================================================
# ui_designer.ps1 —— VortexOS 可视化 UI 设计器(Windows 开发机)
#
# 拖动 6 种组件到客户区画布, 右侧改属性, 一键生成可编译的 C 程序文件.
# 生成代码构建在 libwidget 运行时库之上(src/include/gui/libwidget.h),
# 与 Makefile 的 USER_CFLAGS 一致, 可用现有 compile_tool 或 build_wsl.sh 编译.
#
# 用法:  powershell -STA -ExecutionPolicy Bypass -File tools\ui_designer.ps1
# ============================================================================
# 注: 颜色成员(Fg/Bg/Border)故意不写 [System.Drawing.Color] 类型约束——
#     PS 5.1 在"整文件解析期"就会解析 class 成员的类型, 而 Add-Type 是运行时
#     语句, 此时程序集尚未加载, 带类型约束会报 TypeNotFound。改用无类型成员,
#     运行时由 Add-Type 加载后再赋 Color 对象即可。
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# ---- 控件模型 ----
class WdItem {
    [string]$Name
    [int]$Type            # 0=Button 1=Label 2=TextBox 3=CheckBox 4=Line 5=Rect
    [string]$TypeName
    [int]$X; [int]$Y; [int]$W; [int]$H
    [string]$Text
    $Fg                    # System.Drawing.Color (运行时赋值)
    $Bg
    $Border
    [int]$Cap             # TextBox 容量
    [bool]$Checked        # CheckBox
}

$script:Items = New-Object System.Collections.Generic.List[object]
$script:SelIdx = -1
$script:NextId = @{ Button=1; Label=1; TextBox=1; CheckBox=1; Line=1; Rect=1 }
$script:DragMode = 'none'   # none / move / resize
$script:DragOfs = @{x=0;y=0}
$script:WinH = 240
$script:WinW = 360
$script:BodyBgColor = [System.Drawing.Color]::FromArgb(240,240,240)
$script:TitleColor  = [System.Drawing.Color]::FromArgb(60,80,170)

$TypeCode = @{ Button=0; Label=1; TextBox=2; CheckBox=3; Line=4; Rect=5 }

function Color-Hex([System.Drawing.Color]$c) { ('#{0:X2}{1:X2}{2:X2}' -f $c.R,$c.G,$c.B) }
function Color-Rgb([System.Drawing.Color]$c) { ('guiRgb({0},{1},{2})' -f $c.R,$c.G,$c.B) }

# ---- 添加组件 ----
function Add-Item([string]$t) {
    $i = [WdItem]::new()
    $n = $script:NextId[$t]
    $script:NextId[$t]++
    $i.Name = ($t.ToLower()) + ($script:NextId[$t] - 1)
    $i.Type = $TypeCode[$t]
    $i.TypeName = $t
    $i.X = 20; $i.Y = 20 + ($script:Items.Count % 8) * 22
    $i.Text = switch ($t) { 'Button'{"Button"} 'Label'{"Label"} 'TextBox'{"text"} 'CheckBox'{"Check"} 'Line'{""} 'Rect'{""} }
    $i.Cap = 32; $i.Checked = $true
    switch ($t) {
        'Button'  { $i.W=120; $i.H=36; $i.Fg=[System.Drawing.Color]::White; $i.Bg=[System.Drawing.Color]::FromArgb(60,110,200); $i.Border=[System.Drawing.Color]::FromArgb(20,40,90) }
        'Label'   { $i.W=120; $i.H=20; $i.Fg=[System.Drawing.Color]::FromArgb(40,40,40); $i.Bg=[System.Drawing.Color]::Transparent; $i.Border=[System.Drawing.Color]::Transparent }
        'TextBox' { $i.W=180; $i.H=28; $i.Fg=[System.Drawing.Color]::FromArgb(20,20,20); $i.Bg=[System.Drawing.Color]::White; $i.Border=[System.Drawing.Color]::FromArgb(120,120,120) }
        'CheckBox'{ $i.W=140; $i.H=22; $i.Fg=[System.Drawing.Color]::FromArgb(40,40,40); $i.Bg=[System.Drawing.Color]::White; $i.Border=[System.Drawing.Color]::FromArgb(120,120,120) }
        'Line'    { $i.W=200; $i.H=2;  $i.Border=[System.Drawing.Color]::FromArgb(180,180,180) }
        'Rect'    { $i.W=200; $i.H=60; $i.Border=[System.Drawing.Color]::FromArgb(140,140,140) }
    }
    $script:Items.Add($i)
    $script:SelIdx = $script:Items.Count - 1
    Sync-List; Sync-Props; $script:Canvas.Invalidate()
}

function Sync-List {
    $script:GridList.Rows.Clear()
    for ($k = 0; $k -lt $script:Items.Count; $k++) {
        $it = $script:Items[$k]
        $idx = $script:GridList.Rows.Add($it.Name, $it.TypeName)
        if ($k -eq $script:SelIdx) { $script:GridList.Rows[$idx].Selected = $true }
    }
}

function Sync-Props {
    $script:GridProps.Rows.Clear()
    if ($script:SelIdx -lt 0 -or $script:SelIdx -ge $script:Items.Count) { return }
    $it = $script:Items[$script:SelIdx]
    Add-Prop 'Name'    $it.Name
    Add-Prop 'Text'    $it.Text
    Add-Prop 'X'       $it.X
    Add-Prop 'Y'       $it.Y
    Add-Prop 'W'       $it.W
    Add-Prop 'H'       $it.H
    Add-Prop 'Fg'      (Color-Hex $it.Fg)
    Add-Prop 'Bg'      (Color-Hex $it.Bg)
    Add-Prop 'Border'  (Color-Hex $it.Border)
    if ($it.Type -eq 2) { Add-Prop 'Cap' $it.Cap }
    if ($it.Type -eq 3) { if ($it.Checked) { Add-Prop 'Checked' '1' } else { Add-Prop 'Checked' '0' } }
}
function Add-Prop([string]$k, $v) { $null = $script:GridProps.Rows.Add($k, [string]$v) }

# ---- 绘制画布: 每个控件按类型画预览 ----
function Draw-Canvas {
    $g = $script:Canvas.CreateGraphics()
    $g.Clear([System.Drawing.Color]::FromArgb(200,200,200))   # 画布外
    # 窗口客户区(所见即所得: 超出部分裁掉, 与系统一致)
    $r = New-Object System.Drawing.Rectangle(0,0,[math]::Max(1,$script:WinW),[math]::Max(1,$script:WinH))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($script:BodyBgColor)), $r)
    $g.Clip = $r
    $f = New-Object System.Drawing.Font('Microsoft YaHei',9)
    $fc1 = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(40,40,40))
    $fc2 = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
    for ($k=0; $k -lt $script:Items.Count; $k++) {
        $it = $script:Items[$k]
        $rr = New-Object System.Drawing.Rectangle($it.X,$it.Y,$it.W,$it.H)
        switch ($it.Type) {
            0 { $g.FillRectangle((New-Object System.Drawing.SolidBrush($it.Bg)),$rr); $g.DrawRectangle((New-Object System.Drawing.Pen($it.Border)),$rr); $g.DrawString($it.Text,$f,$fc2,[float]($it.X+6),[float]$it.Y+([math]::Max(0,$it.H-16)/2)) }
            1 { $g.DrawString($it.Text,$f,$fc1,[float]$it.X,[float]$it.Y) }
            2 { $g.FillRectangle((New-Object System.Drawing.SolidBrush($it.Bg)),$rr); $g.DrawRectangle((New-Object System.Drawing.Pen($it.Border)),$rr); $g.DrawString($it.Text,$f,$fc1,[float]($it.X+2),[float]$it.Y+2) }
            3 { $g.FillRectangle((New-Object System.Drawing.SolidBrush($it.Bg)),(New-Object System.Drawing.Rectangle($it.X,$it.Y,14,14))); $g.DrawRectangle((New-Object System.Drawing.Pen($it.Border)),(New-Object System.Drawing.Rectangle($it.X,$it.Y,14,14))); if($it.Checked){ $g.DrawLine((New-Object System.Drawing.Pen($it.Fg,2)),$it.X+2,$it.Y+8,$it.X+5,$it.Y+11); $g.DrawLine((New-Object System.Drawing.Pen($it.Fg,2)),$it.X+5,$it.Y+11,$it.X+12,$it.Y+3) }; $g.DrawString($it.Text,$f,$fc1,[float]($it.X+20),[float]$it.Y) }
            4 { $g.DrawLine((New-Object System.Drawing.Pen($it.Border)),$it.X,$it.Y+$it.H/2,$it.X+$it.W,$it.Y+$it.H/2) }
            5 { $g.DrawRectangle((New-Object System.Drawing.Pen($it.Border)),$rr) }
        }
        if ($k -eq $script:SelIdx) { $g.DrawRectangle((New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(0,140,255),1.5)),$rr) }
    }
    $fc1.Dispose(); $fc2.Dispose(); $f.Dispose(); $g.Dispose()
}

# ---- 命中 ----
function Hit-Test($x,$y) {
    for ($k=$script:Items.Count-1; $k -ge 0; $k--) {
        $it=$script:Items[$k]
        if ($x -ge $it.X -and $x -lt ($it.X+$it.W) -and $y -ge $it.Y -and $y -lt ($it.Y+$it.H)) { return $k }
    }
    return -1
}

# ---- 生成 C 源码 ----
function Get-Generated {
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine('// Generated by ui_designer.ps1 — compilable VortexOS GUI program.')
    $null = $sb.AppendLine('#include <gui/libwidget.h>')
    # TextBox 输入缓冲
    for ($k=0; $k -lt $script:Items.Count; $k++) {
        $it = $script:Items[$k]
        if ($it.Type -eq 2) {
            $null = $sb.AppendLine(('static char {0}_buf[{1}];' -f $it.Name, [math]::Max(2,$it.Cap)))
        }
    }
    $null = $sb.AppendLine('static Widget ws[] = {')
    $count = $script:Items.Count
    for ($k=0; $k -lt $count; $k++) {
        $it = $script:Items[$k]
        $td = @('WD_BUTTON','WD_LABEL','WD_TEXTBOX','WD_CHECKBOX','WD_LINE','WD_RECT')[$it.Type]
        $single = if ($it.Type -eq 2) { ('{0}_buf' -f $it.Name) } else { '0' }
        $scap = if ($it.Type -eq 2) { [string][math]::Max(2,$it.Cap) } else { '0' }
        $chk = if ($it.Type -eq 3) { if ($it.Checked) { '1' } else { '0' } } else { '0' }
        $lbl = '"' + ($it.Text -replace '\\','\\\\' -replace '"','\"') + '"'
        $comma = if ($k -lt $count - 1) { ',' } else { '' }
        $null = $sb.AppendLine(('    {{ {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, 0, {9}, {10}, 0, {11} }}{12}' -f `
            $td, $it.X, $it.Y, $it.W, $it.H, $lbl, (Color-Rgb $it.Fg), (Color-Rgb $it.Bg), (Color-Rgb $it.Border), $single, $scap, $chk, $comma))
    }
    $null = $sb.AppendLine('};')
    $null = $sb.AppendLine(('#define N (sizeof(ws)/sizeof(ws[0]))'))
    $null = $sb.AppendLine('void _start(void) {')
    $null = $sb.AppendLine(('    int win = guiCreateWindow("{0}", 120, 90, {1}, {2}, {3}, {4}, WM_STYLE_DEFAULT);' -f `
        ($script:txtTitle.Text -replace '\\','\\\\' -replace '"','\"'), $script:WinW, $script:WinH, (Color-Rgb $script:TitleColor), (Color-Rgb $script:BodyBgColor)))
    $null = $sb.AppendLine('    if (win < 0) guiExit();')
    $null = $sb.AppendLine('    wdInit(ws, N);')
    $null = $sb.AppendLine('    int focus = -1;')
    $null = $sb.AppendLine('    wdDraw(win, ws, N); guiFlush();')
    $null = $sb.AppendLine('    for (;;) {')
    $null = $sb.AppendLine('        WmEvent ev; guiPoll(&ev);')
    $null = $sb.AppendLine('        if (ev.key == KEY_ESC) break;')
    $null = $sb.AppendLine('        int wx, wy; guiGetWinPos(win, &wx, &wy);')
    $null = $sb.AppendLine('        int mx = ev.mouseX - wx;')
    $null = $sb.AppendLine('        int my = ev.mouseY - wy - WM_TITLEBAR_H;')
    $null = $sb.AppendLine('        if (ev.buttons & GUI_MOUSE_LEFT) {')
    $null = $sb.AppendLine('            int idx = wdHit(ws, N, mx, my);')
    $null = $sb.AppendLine('            if (idx >= 0) { focus = idx; if (wdPress(win, ws, N, idx, ev.leftDown)) { wdDraw(win, ws, N); guiFlush(); } }')
    $null = $sb.AppendLine('            else if (ev.leftDown == 0) focus = -1;')
    $null = $sb.AppendLine('        }')
    $null = $sb.AppendLine('        if (wdHandleKey(win, ws, N, &focus, ev.key)) { wdDraw(win, ws, N); guiFlush(); }')
    $null = $sb.AppendLine('    }')
    $null = $sb.AppendLine('    guiExit();')
    $null = $sb.AppendLine('}')
    return $sb.ToString()
}

# ============================ 构建窗口 ============================
$form = New-Object System.Windows.Forms.Form
$form.Text = 'VortexOS UI 设计器 · 拖动组件生成 C 程序'
$form.Size = New-Object System.Drawing.Size(1080, 760)
$form.MinimumSize = New-Object System.Drawing.Size(960, 660)
$form.StartPosition = 'CenterScreen'

# ---- 顶部: 窗口属性 + 生成 ----
$lblTitle = New-Object System.Windows.Forms.Label; $lblTitle.Text='窗口标题'; $lblTitle.SetBounds(12,16,70,22)
$script:txtTitle = New-Object System.Windows.Forms.TextBox; $script:txtTitle.Text='My UI'; $script:txtTitle.SetBounds(90,14,140,24)
$lblW = New-Object System.Windows.Forms.Label; $lblW.Text='宽'; $lblW.SetBounds(242,16,24,22)
$numW = New-Object System.Windows.Forms.NumericUpDown; $numW.SetBounds(268,14,58,24); $numW.Minimum=100; $numW.Maximum=2048; $numW.Value=$script:WinW
$lblH = New-Object System.Windows.Forms.Label; $lblH.Text='高'; $lblH.SetBounds(336,16,24,22)
$numH = New-Object System.Windows.Forms.NumericUpDown; $numH.SetBounds(362,14,58,24); $numH.Minimum=100; $numH.Maximum=2048; $numH.Value=$script:WinH
$btnBody = New-Object System.Windows.Forms.Button; $btnBody.Text='客户区底色'; $btnBody.SetBounds(430,13,100,27)
$btnTitleC = New-Object System.Windows.Forms.Button; $btnTitleC.Text='标题栏色'; $btnTitleC.SetBounds(538,13,100,27)
$btnGen = New-Object System.Windows.Forms.Button; $btnGen.Text='生成 C 程序…'; $btnGen.Font=New-Object System.Drawing.Font('Microsoft YaHei',9,[System.Drawing.FontStyle]::Bold); $btnGen.SetBounds(900,13,150,30)

$form.Controls.AddRange(@($lblTitle,$script:txtTitle,$lblW,$numW,$lblH,$numH,$btnBody,$btnTitleC,$btnGen))

# ---- 左侧: 添加组件 + 控件列表 ----
$types = @('Button','Label','TextBox','CheckBox','Line','Rect')
for ($i=0; $i -lt $types.Count; $i++) {
    $t = $types[$i]
    $b = New-Object System.Windows.Forms.Button
    $b.Text = "添加 $t"
    $b.SetBounds(14, 60 + $i*32, 170, 28)
    $t2 = $t
    $b.Add_Click({ Add-Item $t2 })
    $form.Controls.Add($b)
}
$script:GridList = New-Object System.Windows.Forms.DataGridView
$script:GridList.SetBounds(12, 260, 174, 380)
$script:GridList.ColumnCount = 2
$script:GridList.Columns[0].HeaderText='名称'; $script:GridList.Columns[1].HeaderText='类型'
$script:GridList.Columns[0].Width=86; $script:GridList.Columns[1].Width=72
$script:GridList.AllowUserToAddRows=$false; $script:GridList.AllowUserToDeleteRows=$false
$script:GridList.RowHeadersVisible=$false
$script:GridList.Add_SelectionChanged({
    if ($script:GridList.SelectedRows.Count -gt 0) { $script:SelIdx = $script:GridList.SelectedRows[0].Index; Sync-Props; $script:Canvas.Invalidate() }
})
$form.Controls.Add($script:GridList)

# ---- 中间: 画布 ----
$script:Canvas = New-Object System.Windows.Forms.Panel
$script:Canvas.SetBounds(196, 60, 640, 580)
$script:Canvas.BackColor = [System.Drawing.Color]::FromArgb(200,200,200)
$script:Canvas.Add_Paint({ Draw-Canvas })
$script:Canvas.Add_MouseDown({
    if ($_.Button -eq 'Right') { if($script:SelIdx -ge 0){ $script:Items.RemoveAt($script:SelIdx); $script:SelIdx=-1; Sync-List; Sync-Props; $script:Canvas.Invalidate() }; return }
    if ($_.Button -ne 'Left') { return }
    $x=$_.X; $y=$_.Y
    $script:SelIdx = Hit-Test $x $y
    if ($script:SelIdx -ge 0) {
        $it=$script:Items[$script:SelIdx]
        # 右下角手柄 → 改大小
        if ($x -ge ($it.X+$it.W-8) -and $y -ge ($it.Y+$it.H-8)) { $script:DragMode='resize' } else { $script:DragMode='move' }
    } else { $script:DragMode='none' }
    if ($script:DragMode -ne 'none') { $script:DragOfs.x = $x - $it.X; $script:DragOfs.y = $y - $it.Y }
    Sync-List; Sync-Props; $script:Canvas.Invalidate()
})
$script:Canvas.Add_MouseMove({
    if ($script:DragMode -eq 'none') { return }
    if ($script:SelIdx -lt 0) { return }
    $it = $script:Items[$script:SelIdx]
    $x=[math]::Max(0,$_.X); $y=[math]::Max(0,$_.Y)
    if ($script:DragMode -eq 'move') {
        $it.X = [math]::Max(0, $x - $script:DragOfs.x)
        $it.Y = [math]::Max(0, $y - $script:DragOfs.y)
    } else {
        $it.W = [math]::Max(20, $x - $it.X)
        $it.H = [math]::Max(10, $y - $it.Y)
    }
    $script:Canvas.Invalidate()
})
$script:Canvas.Add_MouseUp({ $script:DragMode='none' })
$form.Controls.Add($script:Canvas)

# ---- 右侧: 属性面板 ----
$script:GridProps = New-Object System.Windows.Forms.DataGridView
$script:GridProps.SetBounds(846, 60, 216, 580)
$script:GridProps.ColumnCount = 2
$script:GridProps.Columns[0].HeaderText='属性'; $script:GridProps.Columns[1].HeaderText='值'
$script:GridProps.Columns[0].Width=88; $script:GridProps.Columns[1].Width=112
$script:GridProps.AllowUserToAddRows=$false; $script:GridProps.RowHeadersVisible=$false
$script:GridProps.Add_CellEndEdit({
    param($s,$e)
    if ($script:SelIdx -lt 0 -or $script:SelIdx -ge $script:Items.Count) { return }
    $it = $script:Items[$script:SelIdx]
    $prop = $script:GridProps.Rows[$e.RowIndex].Cells[0].Value
    $val  = $script:GridProps.Rows[$e.RowIndex].Cells[1].Value
    try {
        switch ($prop) {
            'Name'   { $it.Name = [string]$val; Sync-List }
            'Text'   { $it.Text = [string]$val }
            'X'      { $it.X = [int]$val }
            'Y'      { $it.Y = [int]$val }
            'W'      { $it.W = [math]::Max(10,[int]$val) }
            'H'      { $it.H = [math]::Max(10,[int]$val) }
            'Fg'     { $it.Fg = [System.Drawing.ColorTranslator]::FromHtml([string]$val) }
            'Bg'     { $it.Bg = [System.Drawing.ColorTranslator]::FromHtml([string]$val) }
            'Border' { $it.Border = [System.Drawing.ColorTranslator]::FromHtml([string]$val) }
            'Cap'    { $it.Cap = [math]::Max(2,[int]$val) }
            'Checked'{ $it.Checked = ([string]$val -eq '1') }
        }
    } catch { }
    $script:Canvas.Invalidate(); Sync-Props
})
# 属性面板的 CellEndEdit 事件(避免脚本块内 $this 混淆)
$form.Controls.Add($script:GridProps)
$form.Add_Shown({ Sync-List; Sync-Props })

# ---- 颜色选择 + 生成 ----
$btnBody.Add_Click({ $c=[System.Windows.Forms.ColorDialog]::new(); $c.Color=$script:BodyBgColor; if($c.ShowDialog() -eq 'OK'){ $script:BodyBgColor=$c.Color; $numW_=$numW.Value; $script:Canvas.Invalidate() } })
$btnTitleC.Add_Click({ $c=[System.Windows.Forms.ColorDialog]::new(); $c.Color=$script:TitleColor; if($c.ShowDialog() -eq 'OK'){ $script:TitleColor=$c.Color; $script:Canvas.Invalidate() } })
$numW.Add_ValueChanged({ $script:WinW=[int]$numW.Value; $script:Canvas.Invalidate() })
$numH.Add_ValueChanged({ $script:WinH=[int]$numH.Value; $script:Canvas.Invalidate() })

$btnGen.Add_Click({
    if ($script:Items.Count -eq 0) { [System.Windows.Forms.MessageBox]::Show('先添加至少一个组件','提示') ; return }
    $dlg = New-Object System.Windows.Forms.SaveFileDialog
    $dlg.Title = '保存生成的 C 程序'
    $dlg.Filter = 'C 源码 (*.c)|*.c'
    $dlg.InitialDirectory = (Join-Path (Split-Path $PSScriptRoot) 'src\user')
    $dlg.FileName = 'myapp.c'
    if ($dlg.ShowDialog() -eq 'OK') {
        [System.IO.File]::WriteAllText($dlg.FileName, (Get-Generated), (New-Object System.Text.UTF8Encoding($false)))
        [System.Windows.Forms.MessageBox]::Show("已生成: $($dlg.FileName)`n`n下一步用 compile_tool 或 build_wsl.sh 编译。", '完成')
    }
})

$form.ShowDialog() | Out-Null