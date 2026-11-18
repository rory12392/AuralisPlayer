# 系統媒體控制（SMTC）的外部探針：從另一個行程經 Windows 的媒體工作階段管理員找到本程式，
# 讀播放狀態或送出播放／暫停、下一首、上一首（等同鍵盤媒體鍵交給本程式的按鈕）。
# 用 SourceAppUserModelId 認出本程式，不會控制到同時開著的其他播放器。
#
# 用法（Windows PowerShell 5.1；它才能直接使用 WinRT 型別）：
#   powershell -NoProfile -File smtc_probe.ps1 -Action list
#   powershell -NoProfile -File smtc_probe.ps1 -Action toggle|next|previous|status [-App AuralisPlayer]
#   powershell -NoProfile -File smtc_probe.ps1 -Action media [-OutDir <資料夾>]
#     讀系統顯示的曲名、藝人、專輯與縮圖（AC-18-2）：縮圖的大小與 SHA-256；給了 -OutDir 時另存成
#     thumb-<雜湊前 12 碼>.png
# 檔案存成 UTF-8 含 BOM：Windows PowerShell 5.1 讀沒有 BOM 的檔案會當成 ANSI，中文註解會壞掉。
# 輸出一行 JSON；找不到本程式的工作階段時結束碼 2，送出的指令被拒絕時結束碼 3。
param(
    [ValidateSet('list', 'status', 'toggle', 'next', 'previous', 'media')]
    [string]$Action = 'list',
    [string]$App = 'AuralisPlayer',
    [string]$OutDir = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1

function Wait-WinRt($operation, [Type]$resultType) {
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($operation))
    $task.Wait() | Out-Null
    return $task.Result
}

$managerType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime]
$manager = Wait-WinRt ($managerType::RequestAsync()) $managerType

$sessions = @($manager.GetSessions())
if ($Action -eq 'list') {
    $rows = foreach ($s in $sessions) {
        [pscustomobject]@{ app = $s.SourceAppUserModelId; status = "$($s.GetPlaybackInfo().PlaybackStatus)" }
    }
    ConvertTo-Json -Compress -InputObject @($rows)
    exit 0
}

$session = $sessions | Where-Object { $_.SourceAppUserModelId -like "*$App*" } | Select-Object -First 1
if ($null -eq $session) {
    Write-Output (ConvertTo-Json -Compress @{ app = $App; found = $false })
    exit 2
}
if ($Action -eq 'media') {
    $propsType = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties, Windows.Media.Control, ContentType = WindowsRuntime]
    $props = Wait-WinRt ($session.TryGetMediaPropertiesAsync()) $propsType
    $thumb = $null
    if ($null -ne $props.Thumbnail) {
        $streamType = [Windows.Storage.Streams.IRandomAccessStreamWithContentType, Windows.Storage.Streams, ContentType = WindowsRuntime]
        $stream = Wait-WinRt ($props.Thumbnail.OpenReadAsync()) $streamType
        $net = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
        $memory = New-Object System.IO.MemoryStream
        $net.CopyTo($memory)
        $bytes = $memory.ToArray()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $hash = -join ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
        $file = ''
        if ($OutDir -ne '') {
            New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
            $file = Join-Path $OutDir ("thumb-" + $hash.Substring(0, 12) + ".png")
            [System.IO.File]::WriteAllBytes($file, $bytes)
        }
        $thumb = @{ bytes = $bytes.Length; sha256 = $hash; file = $file }
    }
    Write-Output (ConvertTo-Json -Compress @{
            app       = $session.SourceAppUserModelId
            found     = $true
            action    = $Action
            status    = "$($session.GetPlaybackInfo().PlaybackStatus)"
            title     = $props.Title
            artist    = $props.Artist
            album     = $props.AlbumTitle
            thumbnail = $thumb
        })
    exit 0
}
$accepted = $true
switch ($Action) {
    'toggle' { $accepted = Wait-WinRt ($session.TryTogglePlayPauseAsync()) ([bool]) }
    'next' { $accepted = Wait-WinRt ($session.TrySkipNextAsync()) ([bool]) }
    'previous' { $accepted = Wait-WinRt ($session.TrySkipPreviousAsync()) ([bool]) }
}
$info = $session.GetPlaybackInfo()
Write-Output (ConvertTo-Json -Compress @{
        app      = $session.SourceAppUserModelId
        found    = $true
        action   = $Action
        accepted = $accepted
        status   = "$($info.PlaybackStatus)"
    })
if (-not $accepted) { exit 3 }
exit 0
