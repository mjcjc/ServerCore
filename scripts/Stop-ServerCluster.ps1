$clusterPorts = @(6000, 6501, 7001, 7002, 7003)
$listeners = Get-NetTCPConnection `
    -State Listen `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.LocalPort -in $clusterPorts }

if ($null -eq $listeners) {
    Write-Host "No local server cluster is running."
    return
}

$processes = $listeners |
    Select-Object -ExpandProperty OwningProcess -Unique |
    ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue } |
    Where-Object { $_.ProcessName -in @("MasterServer", "WorldServer", "TCPServer") }

foreach ($process in $processes) {
    Write-Host "Stopping $($process.ProcessName) (PID $($process.Id))..."
    Stop-Process -Id $process.Id -Confirm:$false
}

Write-Host "Server cluster stopped."
