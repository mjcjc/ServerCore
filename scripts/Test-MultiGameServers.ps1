param(
    [string]$Executable = "..\x64\Debug\TCPStressClient.exe",
    [int]$ClientsPerServer = 20,
    [int]$SendersPerServer = 4,
    [int]$MessagesPerSecond = 5,
    [int]$DurationSeconds = 5
)

$stressClient = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot $Executable)
)

if (-not (Test-Path -LiteralPath $stressClient)) {
    throw "TCPStressClient executable was not found: $stressClient"
}

$ports = @(7001, 7002, 7003)
$unavailablePorts = @()

foreach ($port in $ports) {
    $probe = [System.Net.Sockets.TcpClient]::new()
    try {
        $probe.Connect("127.0.0.1", $port)
    }
    catch {
        $unavailablePorts += $port
    }
    finally {
        $probe.Dispose()
    }
}

if ($unavailablePorts.Count -gt 0) {
    throw "GameServer is not listening on port(s): $($unavailablePorts -join ', '). Run .\scripts\Start-GameServers.ps1 first."
}

$resultDirectory = Join-Path $PSScriptRoot "..\test-results\multi-game-server"
New-Item -ItemType Directory -Path $resultDirectory -Force | Out-Null

$tests = @()

foreach ($port in $ports) {
    $outputPath = Join-Path $resultDirectory "server-$port.log"
    $errorPath = Join-Path $resultDirectory "server-$port.error.log"

    $process = Start-Process `
        -FilePath $stressClient `
        -ArgumentList @(
            "127.0.0.1",
            $port,
            $ClientsPerServer,
            $SendersPerServer,
            $MessagesPerSecond,
            $DurationSeconds
        ) `
        -WorkingDirectory (Split-Path -Parent $stressClient) `
        -RedirectStandardOutput $outputPath `
        -RedirectStandardError $errorPath `
        -WindowStyle Hidden `
        -PassThru

    $tests += [pscustomobject]@{
        Port = $port
        Process = $process
        OutputPath = $outputPath
        ErrorPath = $errorPath
    }
}

$failed = $false

foreach ($test in $tests) {
    $test.Process.WaitForExit()
    $test.Process.Refresh()
    $output = Get-Content -LiteralPath $test.OutputPath -Raw
    $errors = Get-Content -LiteralPath $test.ErrorPath -Raw
    $passed = $test.Process.ExitCode -eq 0

    if (-not $passed) {
        $failed = $true
    }

    Write-Host ""
    Write-Host "========== GameServer port $($test.Port): $(if ($passed) { 'PASS' } else { 'FAIL' }) =========="
    Write-Host $output.TrimEnd()

    if (-not [string]::IsNullOrWhiteSpace($errors)) {
        Write-Error $errors.TrimEnd()
    }
}

Write-Host ""
if ($failed) {
    Write-Host "Multi-GameServer stress test: FAIL"
    exit 1
}

Write-Host "Multi-GameServer stress test: PASS"
Write-Host "Detailed logs: $resultDirectory"
exit 0
