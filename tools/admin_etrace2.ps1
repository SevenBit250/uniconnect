$log = "C:\Users\25293\Desktop\aiworkspace\uniconnect\tools\admin_etrace2_result.txt"
$etl = "C:\Users\25293\Desktop\aiworkspace\uniconnect\tools\usbtrace2.etl"
Remove-Item $log, $etl -ErrorAction SilentlyContinue

logman stop UsbTrace2 -ets 2>$null | Out-Null
logman start UsbTrace2 -ets -ow -o $etl -nb 256 2048 -bs 256 | Out-File $log
foreach ($prov in 'Microsoft-Windows-USB-USBHUB3','Microsoft-Windows-USB-USBCCGP','Microsoft-Windows-USB-USBXHCI','Microsoft-Windows-Kernel-PnP') {
    logman update UsbTrace2 -ets -p $prov 0xffffffffffffffff 255 2>&1 | Out-Null
}
"trace started (hub3+ccgp+xhci+pnp), waiting 50s" | Out-File $log -Append
Start-Sleep 50
logman stop UsbTrace2 -ets | Out-File $log -Append

"=== 0D28 设备全事件(不过滤级别) ===" | Out-File $log -Append
try {
    $ev = Get-WinEvent -Path $etl -Oldest -ErrorAction Stop
    "total events: $($ev.Count)" | Out-File $log -Append
    $n = 0
    foreach ($e in $ev) {
        $msg = if ($e.Message) { $e.Message -replace "`r`n", ' ' } else { '' }
        $props = ($e.Properties | ForEach-Object { "$($_.Value)" }) -join ' '
        $line = "[$($e.TimeCreated.ToString('HH:mm:ss.fff'))] $($e.ProviderName -replace 'Microsoft-Windows-','') Id=$($e.Id) L$($e.Level) $msg $props"
        if ($line -match '0D28') {
            $line.Substring(0, [Math]::Min(400, $line.Length)) | Out-File $log -Append
            $n++
        }
    }
    "0D28命中: $n" | Out-File $log -Append
    # PnP 配置事件(4xx)
    "=== Kernel-PnP 配置事件(最近) ===" | Out-File $log -Append
    $ev | Where-Object { $_.ProviderName -match 'Kernel-PnP' -and $_.Id -in 400,410,411,420,430 } |
        Select-Object -Last 15 | ForEach-Object {
            $m = if ($_.Message) { $_.Message -replace "`r`n", ' ' } else { '' }
            "[$($_.TimeCreated.ToString('HH:mm:ss'))] Id=$($_.Id) $m" | Out-File $log -Append
        }
} catch {
    "ETL解析失败: $_" | Out-File $log -Append
}
"DONE" | Out-File $log -Append
