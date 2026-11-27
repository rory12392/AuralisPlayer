# P-41: windows-platform screenshot whose window can never be drawn (started hidden, the same as a
# switched-off or locked screen) must fail with a reason instead of waiting for frames forever.
# Prints "rc=<exit code>" and the program's stderr bytes; ctest matches them. ASCII only, so that
# Windows PowerShell 5.1 reads this file the same under any code page.
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$DataDir,
    [Parameter(Mandatory)][string]$Out,
    [int]$WaitSeconds = 120
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $DataDir | Out-Null
$err = Join-Path $DataDir 'stderr.txt'
$log = Join-Path $DataDir 'stdout.txt'
$env:QT_QPA_PLATFORM = 'windows'
$p = Start-Process -FilePath $Exe -WindowStyle Hidden -PassThru `
    -ArgumentList '--data-dir', $DataDir, '--screenshot', 'shell', '--size', '960x640', '--out', $Out `
    -RedirectStandardError $err -RedirectStandardOutput $log
# Without touching Handle first, ExitCode can come back empty after WaitForExit(timeout)
$null = $p.Handle
if (-not $p.WaitForExit($WaitSeconds * 1000)) {
    $p.Kill()
    Write-Output "still running after $WaitSeconds s"
    exit 1
}
Write-Output "rc=$($p.ExitCode)"
$bytes = [IO.File]::ReadAllBytes($err)
$stdout = [Console]::OpenStandardOutput()
$stdout.Write($bytes, 0, $bytes.Length)
$stdout.Flush()
