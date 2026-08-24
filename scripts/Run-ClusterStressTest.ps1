param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [int[]]$Ports = @(7001, 7002, 7003),
    [int]$ClientsPerServer = 100,
    [int]$SendersPerServer = 5,
    [int]$MessagesPerSecond = 2,
    [int]$DurationSeconds = 10
)

$stressClient = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\x64\$Configuration\TCPStressClient.exe")
)
$expectedGameServer = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\x64\$Configuration\TCPServer.exe")
)

if (-not (Test-Path -LiteralPath $stressClient)) {
    throw "TCPStressClient executable was not found: $stressClient"
}

if ($ClientsPerServer -le 0 -or
    $SendersPerServer -le 0 -or
    $SendersPerServer -gt $ClientsPerServer -or
    $MessagesPerSecond -le 0 -or
    $DurationSeconds -le 0) {
    throw "Stress-test values must be positive and senders cannot exceed clients."
}

foreach ($port in $Ports) {
    $listener = Get-NetTCPConnection `
        -State Listen `
        -LocalPort $port `
        -ErrorAction SilentlyContinue

    if ($null -eq $listener) {
        throw "No GameServer is listening on port $port."
    }

    $processId = $listener |
        Select-Object -First 1 -ExpandProperty OwningProcess
    $serverProcess = Get-Process -Id $processId -ErrorAction SilentlyContinue

    if ($null -eq $serverProcess -or [string]::IsNullOrWhiteSpace($serverProcess.Path)) {
        throw "Could not resolve the GameServer process on port $port."
    }

    if (-not $serverProcess.Path.Equals(
            $expectedGameServer,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "GameServer on port $port is not the requested $Configuration build: $($serverProcess.Path)"
    }
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$resultDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\test-results\cluster-$timestamp")
)
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null

$runningTests = @()

foreach ($port in $Ports) {
    $standardOutput = Join-Path $resultDirectory "gameserver-$port.txt"
    $standardError = Join-Path $resultDirectory "gameserver-$port-error.txt"
    $arguments = @(
        "127.0.0.1",
        $port,
        $ClientsPerServer,
        $SendersPerServer,
        $MessagesPerSecond,
        $DurationSeconds
    )

    $process = Start-Process `
        -FilePath $stressClient `
        -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $stressClient) `
        -RedirectStandardOutput $standardOutput `
        -RedirectStandardError $standardError `
        -PassThru

    $runningTests += [PSCustomObject]@{
        Port = $port
        Process = $process
        Output = $standardOutput
        ErrorOutput = $standardError
    }

    Write-Host "Started stress test for GameServer port $port (PID $($process.Id))."
}

$failedTests = 0

foreach ($test in $runningTests) {
    $test.Process.WaitForExit()
    $exitCode = $test.Process.ExitCode

    if ($exitCode -ne 0) {
        ++$failedTests
    }

    Write-Host ""
    Write-Host "===== GameServer $($test.Port) / ExitCode $exitCode ====="
    Get-Content -LiteralPath $test.Output

    if ((Get-Item -LiteralPath $test.ErrorOutput).Length -gt 0) {
        Write-Host "----- stderr -----"
        Get-Content -LiteralPath $test.ErrorOutput
    }
}

$totalClients = $ClientsPerServer * $Ports.Count

Write-Host ""
Write-Host "===== Cluster Test Summary ====="
Write-Host "Configuration:       $Configuration"
Write-Host "GameServers:         $($Ports.Count)"
Write-Host "Clients per server:  $ClientsPerServer"
Write-Host "Total clients:       $totalClients"
Write-Host "Senders per server:  $SendersPerServer"
Write-Host "Messages/sec/sender: $MessagesPerSecond"
Write-Host "Duration:            $DurationSeconds sec"
Write-Host "Failed server tests: $failedTests"
Write-Host "Result directory:    $resultDirectory"

if ($failedTests -ne 0) {
    exit 1
}

exit 0
