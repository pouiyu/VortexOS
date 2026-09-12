# vmware_pipe_reader.ps1
# 连接 VMware 命名管道串口(vortex_serial)，持续把 guest 串口输出追加到日志文件。
# 用法: powershell -NoProfile -ExecutionPolicy Bypass -File vmware_pipe_reader.ps1
$pipeName = "vortex_serial"
$logFile  = "c:\Users\Administrator\Documents\OperatingSystem\vortex-os\vmware_pipe.log"
$sb = New-Object System.Text.StringBuilder
while ($true) {
    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(
            ".", $pipeName,
            [System.IO.Pipes.PipeDirection]::In,
            [System.IO.Pipes.PipeOptions]::None)
        $pipe.Connect(120000)   # 等待 VMware server 端点创建
        $reader = New-Object System.IO.StreamReader($pipe)
        while ($true) {
            $char = $reader.Read()
            if ($char -lt 0) { break }
            [void]$sb.Append([char]$char)
            if ($sb.Length -ge 256) {
                [System.IO.File]::AppendAllText($logFile, $sb.ToString())
                $sb.Clear()
            }
        }
    } catch {
        [System.IO.File]::AppendAllText($logFile, "[reader] wait/retry: $($_.Exception.Message)`r`n")
        Start-Sleep -Milliseconds 2000
    }
}