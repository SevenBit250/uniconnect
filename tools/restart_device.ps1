$dev = Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_0D28*' -and $_.Status -eq 'OK' } | Select-Object -First 1
if (-not $dev) {
    $dev = Get-PnpDevice | Where-Object { $_.InstanceId -like '*VID_0D28*' } | Select-Object -First 1
}
if ($dev) {
    Write-Output "找到设备: $($dev.InstanceId) [Status=$($dev.Status)]"
    Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
    Start-Sleep -Milliseconds 800
    Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false
    Start-Sleep -Milliseconds 1500
    $dev2 = Get-PnpDevice | Where-Object { $_.InstanceId -eq $dev.InstanceId }
    Write-Output "重启后状态: $($dev2.Status)"
} else {
    Write-Output "未找到 VID_0D28 设备"
}
