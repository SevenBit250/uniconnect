Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like '*VID_0D28*' } | ForEach-Object {
    Write-Output ("设备: " + $_.InstanceId)
    Write-Output ("状态: " + $_.Status + "  问题码: " + $_.Problem)
    Write-Output ("友好名: " + $_.FriendlyName)
    Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_ProblemCode','DEVPKEY_Device_DriverDesc' -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Output ("  " + $_.KeyName + " = " + $_.Data)
    }
}
