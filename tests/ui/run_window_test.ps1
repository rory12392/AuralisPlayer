# Runs a windows-platform Qt Quick Test (a GUI program: ctest cannot see its output) with its result
# written to a file, then prints that file so ctest shows the frame log, failures and SKIP reasons.
# Start-Process keeps the window visible; cmake's execute_process would start it hidden and the
# test would wait for windowShown forever. ASCII only (Windows PowerShell 5.1, any code page).
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Qml,
    [Parameter(Mandatory)][string]$Out,
    [int]$WaitSeconds = 100
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
Remove-Item $Out -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -PassThru -ArgumentList '-input', "`"$Qml`"", '-o', "`"$Out,txt`""
# Without touching Handle first, ExitCode can come back empty after WaitForExit(timeout)
$null = $p.Handle
$finished = $p.WaitForExit($WaitSeconds * 1000)
if (-not $finished) {
    $p.Kill()
    $p.WaitForExit()
}
if (Test-Path $Out) {
    $bytes = [IO.File]::ReadAllBytes($Out)
    $stdout = [Console]::OpenStandardOutput()
    $stdout.Write($bytes, 0, $bytes.Length)
    $stdout.Flush()
}
if (-not $finished) {
    Write-Output "still running after $WaitSeconds s"
    exit 1
}
exit $p.ExitCode
