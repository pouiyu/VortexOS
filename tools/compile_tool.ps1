# ============================================================================
# compile_tool.ps1 —— VortexOS 用户程序 C→ELF 可视化编译工具(Windows 开发机)
#
# 作用：图形窗口里浏览/选择 src\user\ 下的 .c 源文件，调用 WSL 的交叉 gcc
#   (与 Makefile USER_CFLAGS 一致)编译，再链接成独立 ELF，输出到
#   system\programs\<名>.elf。另提供“打包 ISO”按钮(调 build_wsl.sh)。
#
# 用法(在 PowerShell 窗口内)：
#   powershell -STA -ExecutionPolicy Bypass -File tools\compile_tool.ps1
#   # 或直接:  powershell -STA -File tools\compile_tool.ps1
#
# 前置：本机已装 WSL，且 WSL 内具备 gcc/ld(nasm 非必需)；项目无空格路径。
# ============================================================================
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = 'Stop'
[System.Windows.Forms.Application]::EnableVisualStyles()

# ---- 项目根与 WSL 路径 ----
$script:Root     = Split-Path -Parent $PSScriptRoot          # ...\vortex-os
$script:SrcDir   = Join-Path $script:Root 'src\user'          # 源文件目录(默认)
$script:OutDir   = Join-Path $script:Root 'system\programs'   # ELF 输出目录

$script:WslRoot  = ('/mnt/' + (($script:Root.Substring(0,1)).ToLower()) + ($script:Root.Substring(2) -replace '\\','/'))

# ---- 日志：追加一行到日志框(须在 UI 线程调用) ----
function Add-Log([string]$text) {
    $script:LogBox.AppendText($text + [Environment]::NewLine)
    $script:LogBox.ScrollToCaret()
}

# ---- 在后台线程跑一条 WSL bash 命令，完成后回 UI 线程 ----
function Invoke-WslAsync([string]$title, [string]$bashCmd) {
    Add-Log ''
    Add-Log ('==   ' + $title + '  ==')
    Add-Log ('$ ' + $bashCmd)

    $thread = [System.Threading.Tasks.Task]::Run({
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = 'wsl.exe'
        $psi.Arguments = ('-- bash -lc ' + '"' + $bashCmd + '"')
        $psi.WorkingDirectory = $script:Root
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError  = $true
        $psi.CreateNoWindow = $true
        $p = $null
        try { $p = [System.Diagnostics.Process]::Start($psi) }
        catch { return "启动失败(WSL 不可用?): $_" }
        $out = $p.StandardOutput.ReadToEnd()
        $err = $p.StandardError.ReadToEnd()
        $p.WaitForExit()
        $code = $p.ExitCode
        $all = if ($out) { $out.TrimEnd("`r`n") } else { '' }
        if ($err) { if ($all) { $all += "`r`n" }; $all += $err.TrimEnd("`r`n") }
        return ("## 退出码 " + $code + ($(if ($code -eq 0) { ' (成功)' } else { ' (失败)!!' })) +
                ($(if ($all) { "`r`n" + $all } else { '' })))
    })

    $script:LogBox.Invoke([Action]{
        $r = $thread.Result
        Add-Log $r
        if ($r -match '成功') { [System.Windows.Forms.MessageBox]::Show($script:Root, '完成', 'VortexOS 编译工具', 'OK', 'Information') }
        else { [System.Windows.Forms.MessageBox]::Show($r, '编译失败', 'VortexOS 编译工具', 'OK', 'Error') }
    })
}

# ---- 编译选中 .c → .elf ----
function Compile-Selected {
    $item = $script:ListBox.SelectedItem
    if (-not $item) { [System.Windows.Forms.MessageBox]::Show('请先在左侧选择一个 .c 源文件', '提示', 'OK', 'Warning'); return }

    $cName = $item
    $elfName = ($cName -replace '\.c$','') + '.elf'
    $progName = ($cName -replace '\.c$','')

    # 委托 WSL 内 make 构建(见 Makefile 的 program 目标, 自动带 libgui+libwidget)
    $cmd = 'cd ' + $script:WslRoot + ' && make program NAME=' + $progName

    Invoke-WslAsync ("make program NAME={0} → {1}" -f $progName, $elfName) $cmd
}

# ---- 重扫源目录刷新列表 ----
function Refresh-Files {
    $script:ListBox.BeginUpdate()
    $script:ListBox.Items.Clear()
    if (Test-Path $script:SrcDir) {
        Get-ChildItem -Path $script:SrcDir -Filter *.c -File | Sort-Object Name | ForEach-Object {
            $null = $script:ListBox.Items.Add($_.Name)
        }
    }
    if ($script:ListBox.Items.Count -gt 0) { $script:ListBox.SelectedIndex = 0 }
    $script:ListBox.EndUpdate()

    if (Test-Path $script:OutDir) {
        $have = @(Get-ChildItem $script:OutDir -Filter *.elf -File).Count
        Add-Log ("已就绪：{0} 个 .c 源文件；system/programs 现有 {1} 个 .elf。" -f $script:ListBox.Items.Count, $have)
    }
}

# ---- 文件预览(在 UI 线程被 ListBox 选中事件触发) ----
function Show-Preview($fileName) {
    if (-not $fileName) { $script:SourceBox.Text = ''; return }
    $path = Join-Path $script:SrcDir $fileName
    try {
        if (Test-Path $path) {
            $script:SourceBox.Text = Get-Content -Path $path -Raw
        } else {
            $script:SourceBox.Text = '(文件不存在: ' + $path + ')'
        }
    } catch {
        $script:SourceBox.Text = '(读取失败: ' + $_.Exception.Message + ')'
    }
}

# ============================ 构建窗口 ============================
$script:Form = New-Object System.Windows.Forms.Form
$script:Form.Text = 'VortexOS · C→ELF 编译工具'
$script:Form.Size = New-Object System.Drawing.Size(880, 640)
$script:Form.MinimumSize = New-Object System.Drawing.Size(760, 520)
$script:Form.StartPosition = 'CenterScreen'

# --- 源文件列表(左) ---
$sourceGroup = New-Object System.Windows.Forms.GroupBox
$sourceGroup.Text = '源文件 (src\user)'
$sourceGroup.SetBounds(12, 12, 220, 400)

$script:ListBox = New-Object System.Windows.Forms.ListBox
$script:ListBox.SetBounds(10, 22, 200, 370)
$script:ListBox.Add_SelectedIndexChanged({ Show-Preview $script:ListBox.SelectedItem })
$sourceGroup.Controls.Add($script:ListBox)
$script:Form.Controls.Add($sourceGroup)

# --- 源码预览(右上) ---
$previewGroup = New-Object System.Windows.Forms.GroupBox
$previewGroup.Text = '源码预览'
$previewGroup.SetBounds(246, 12, 614, 400)

$script:SourceBox = New-Object System.Windows.Forms.RichTextBox
$script:SourceBox.SetBounds(10, 22, 594, 370)
$script:SourceBox.ReadOnly = $true
$script:SourceBox.Font = New-Object System.Drawing.Font('Consolas', 9.0)
$script:SourceBox.HideSelection = $false
$previewGroup.Controls.Add($script:SourceBox)
$script:Form.Controls.Add($previewGroup)

# --- 按钮行 ---
$btnCompile = New-Object System.Windows.Forms.Button
$btnCompile.Text = '编译选中 (.c → .elf)'
$btnCompile.SetBounds(12, 424, 170, 34)
$btnCompile.Add_Click({ Compile-Selected })
$script:Form.Controls.Add($btnCompile)

$btnPickDir = New-Object System.Windows.Forms.Button
$btnPickDir.Text = '选择源目录…'
$btnPickDir.SetBounds(192, 424, 130, 34)
$btnPickDir.Add_Click({
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = '选择含 .c 用户程序的目录'
    $dlg.SelectedPath = $script:SrcDir
    if ($dlg.ShowDialog($script:Form) -eq 'OK') {
        $script:SrcDir = $dlg.SelectedPath
        $sourceGroup.Text = '源文件 (' + $script:SrcDir.Replace($script:Root, '…') + ')'
        Refresh-Files
    }
})
$script:Form.Controls.Add($btnPickDir)

$btnRefresh = New-Object System.Windows.Forms.Button
$btnRefresh.Text = '刷新'
$btnRefresh.SetBounds(332, 424, 90, 34)
$btnRefresh.Add_Click({ Refresh-Files })
$script:Form.Controls.Add($btnRefresh)

$btnPack = New-Object System.Windows.Forms.Button
$btnPack.Text = '打包 ISO (build_wsl.sh)'
$btnPack.SetBounds(614, 424, 166, 34)
$btnPack.Add_Click({
    Invoke-WslAsync '打包 ISO' 'bash tools/build_wsl.sh'
})
$script:Form.Controls.Add($btnPack)

$btnOpen = New-Object System.Windows.Forms.Button
$btnOpen.Text = '打开程序目录'
$btnOpen.SetBounds(790, 424, 70, 34)
$btnOpen.Add_Click({ if (Test-Path $script:OutDir) { explorer.exe $script:OutDir } })
$script:Form.Controls.Add($btnOpen)

# --- 日志(底) ---
$logGroup = New-Object System.Windows.Forms.GroupBox
$logGroup.Text = '编译日志'
$logGroup.SetBounds(12, 472, 848, 128)

$script:LogBox = New-Object System.Windows.Forms.RichTextBox
$script:LogBox.SetBounds(10, 22, 828, 98)
$script:LogBox.ReadOnly = $true
$script:LogBox.BackColor = [System.Drawing.Color]::Black
$script:LogBox.ForeColor = [System.Drawing.Color]::LimeGreen
$script:LogBox.Font = New-Object System.Drawing.Font('Consolas', 9.0)
$script:LogBox.WordWrap = $false
$logGroup.Controls.Add($script:LogBox)
$script:Form.Controls.Add($logGroup)

# --- 启动 ---
$script:Form.Add_Shown({ Refresh-Files })
$script:Form.ShowDialog() | Out-Null