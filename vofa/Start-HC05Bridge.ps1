$ErrorActionPreference = 'Stop'
$bridgeRoot = Split-Path -Parent $PSScriptRoot
$bridgePidPath = Join-Path $PSScriptRoot 'bridge.pid'
if (Test-Path -LiteralPath $bridgePidPath) {
    $bridgeSavedPid = [int](Get-Content -LiteralPath $bridgePidPath)
    $bridgeExisting = Get-CimInstance Win32_Process -Filter "ProcessId=$bridgeSavedPid"
    if ($bridgeExisting -and $bridgeExisting.CommandLine -like '*ble_vofa_bridge.py*') {
        Write-Host 'HC-05 bridge is already running. VOFA+: TCP Client 127.0.0.1:8765 / JustFloat'
        exit 0
    }
}
$bridgePython = 'C:\Program Files\LibreOffice\program\python.exe'
if (-not (Test-Path -LiteralPath $bridgePython)) {
    throw 'Python 3.12 runtime not found. Update bridgePython in this script.'
}
$bridgeProcess = Start-Process -FilePath $bridgePython -ArgumentList @('-u', 'vofa/ble_vofa_bridge.py', '--paired', '--mode', 'serve') -WorkingDirectory $bridgeRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $PSScriptRoot 'bridge.log') -RedirectStandardError (Join-Path $PSScriptRoot 'bridge-error.log') -PassThru
$bridgeProcess.Id | Set-Content -LiteralPath $bridgePidPath
Write-Host "HC-05 bridge started (PID $($bridgeProcess.Id))."
Write-Host 'VOFA+: TCP Client 127.0.0.1:8765 / JustFloat. See bridge.log for status.'
