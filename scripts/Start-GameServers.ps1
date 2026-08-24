param(
    [string]$Executable = "..\x64\Debug\TCPServer.exe",
    [int]$WorkerCount = 4,
    [string]$ConnectorDllDirectory = "C:\Program Files\MySQL\MySQL Connector C++ 9.5\lib64",
    [switch]$WithDatabase
)

if ([System.IO.Path]::IsPathRooted($Executable)) {
    $resolvedExecutable = [System.IO.Path]::GetFullPath($Executable)
}
else {
    $resolvedExecutable = [System.IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot $Executable)
    )
}

if (-not (Test-Path -LiteralPath $resolvedExecutable)) {
    throw "TCPServer executable was not found: $resolvedExecutable"
}

if (Test-Path -LiteralPath $ConnectorDllDirectory) {
    $env:PATH = $ConnectorDllDirectory + ";" + $env:PATH
}

$servers = @(
    @{ Id = 1; WorldId = 1; ChannelId = 1; Port = 7001 },
    @{ Id = 2; WorldId = 1; ChannelId = 2; Port = 7002 },
    @{ Id = 3; WorldId = 1; ChannelId = 3; Port = 7003 }
)

foreach ($server in $servers) {
    $listener = Get-NetTCPConnection `
        -State Listen `
        -LocalPort $server.Port `
        -ErrorAction SilentlyContinue

    if ($null -ne $listener) {
        $processId = $listener | Select-Object -First 1 -ExpandProperty OwningProcess
        $processName = Get-Process -Id $processId -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty ProcessName

        Write-Host "GameServer $($server.Id) was not started: port $($server.Port) is already used by $processName (PID $processId)."
        continue
    }

    $arguments = @(
        "--server-id", $server.Id,
        "--world-id", $server.WorldId,
        "--channel-id", $server.ChannelId,
        "--bind-ip", "127.0.0.1",
        "--port", $server.Port,
        "--worker-count", $WorkerCount,
        "--world-server-ip", "127.0.0.1",
        "--world-server-port", 6501
    )

    if (-not $WithDatabase) {
        $arguments += "--skip-db"
    }

    Start-Process `
        -FilePath $resolvedExecutable `
        -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $resolvedExecutable)

    Write-Host "GameServer $($server.Id) started on port $($server.Port)."
}
