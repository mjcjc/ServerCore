param(
    [string]$Configuration = "Debug",
    [int]$WorkerCount = 4,
    [switch]$WithDatabase
)

$outputDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot "..\x64\$Configuration")
)
$masterExecutable = Join-Path $outputDirectory "MasterServer.exe"
$gameServerExecutable = Join-Path $outputDirectory "TCPServer.exe"

if (-not (Test-Path -LiteralPath $masterExecutable)) {
    throw "MasterServer executable was not found: $masterExecutable"
}

if (-not (Test-Path -LiteralPath $gameServerExecutable)) {
    throw "TCPServer executable was not found: $gameServerExecutable"
}

$masterListener = Get-NetTCPConnection `
    -State Listen `
    -LocalPort 6000 `
    -ErrorAction SilentlyContinue

if ($null -eq $masterListener) {
    Start-Process `
        -FilePath $masterExecutable `
        -WorkingDirectory $outputDirectory

    Start-Sleep -Milliseconds 500
}
else {
    Write-Host "MasterServer is already listening on port 6000."
}

$startGameServers = Join-Path $PSScriptRoot "Start-GameServers.ps1"
& $startGameServers `
    -Executable $gameServerExecutable `
    -WorkerCount $WorkerCount `
    -WithDatabase:$WithDatabase
