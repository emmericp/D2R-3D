[CmdletBinding()]
param(
    [ValidateSet('Status','Hold','Restore')][string]$Action='Status',
    [ValidateRange(-85,60)][double]$Pitch=-12,
    [ValidateRange(3.5,1500)][double]$Distance=28,
    [ValidateRange(-180,180)][double]$Yaw=0,
    [int]$GamePid=0,
    [string]$GameDirectory='C:\Program Files (x86)\Diablo II Resurrected'
)
$ErrorActionPreference='Stop'
$taskGameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$taskExpectedExe=[IO.Path]::GetFullPath((Join-Path $taskGameRoot 'D2RLoader.exe'))
$taskProcesses=@(Get-Process D2RLoader -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.Equals($taskExpectedExe,[StringComparison]::OrdinalIgnoreCase) -and
    ($GamePid -eq 0 -or $_.Id -eq $GamePid)
})
if ($taskProcesses.Count -ne 1) { throw 'Select exactly one running modded game using -GamePid. The normal launcher is not controlled.' }
$taskSelectedPid=$taskProcesses[0].Id
$taskBridgeRoot=Join-Path $taskGameRoot 'd2rloader\inspection'
$taskStatusPath=Join-Path $taskBridgeRoot "camera-$taskSelectedPid-status.json"
$taskRequestPath=Join-Path $taskBridgeRoot "camera-$taskSelectedPid-request.txt"
if (-not (Test-Path -LiteralPath $taskStatusPath)) {
    throw 'Camera inspection is not available. Camera 1.4.2 must be installed; enable its optional interface with the in-game command 3dcam_inspection.'
}
$taskRuntime=Get-Content -LiteralPath $taskStatusPath -Raw | ConvertFrom-Json
if ($taskRuntime.process_id -ne $taskSelectedPid -or -not $taskRuntime.inspection_enabled) {
    throw 'The selected game has camera inspection disabled. Use 3dcam_inspection to enable it.'
}
$taskSequence=[uint64][DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
if ($taskSequence -le [uint64]$taskRuntime.sequence) { $taskSequence=[uint64]$taskRuntime.sequence+1 }
$taskCommand="$taskSequence $($taskRuntime.session) $($Action.ToLowerInvariant())"
if ($Action -eq 'Hold') {
    if (-not [double]::IsFinite($Pitch) -or -not [double]::IsFinite($Distance) -or -not [double]::IsFinite($Yaw)) {
        throw 'Camera angles and distance must be finite.'
    }
    $taskCulture=[Globalization.CultureInfo]::InvariantCulture
    $taskCommand+=' '+$Pitch.ToString('R',$taskCulture)+' '+$Distance.ToString('R',$taskCulture)+' '+$Yaw.ToString('R',$taskCulture)
}
$taskTemporary=$taskRequestPath+'.tmp'
[IO.File]::WriteAllText($taskTemporary,$taskCommand,[Text.UTF8Encoding]::new($false))
[IO.File]::Move($taskTemporary,$taskRequestPath,$true)
$taskDeadline=[Diagnostics.Stopwatch]::StartNew()
while ($taskDeadline.Elapsed.TotalSeconds -lt 4) {
    try { $taskReply=Get-Content -LiteralPath $taskStatusPath -Raw | ConvertFrom-Json } catch { $taskReply=$null }
    if ($taskReply -and [uint64]$taskReply.sequence -eq $taskSequence -and [uint64]$taskReply.session -eq [uint64]$taskRuntime.session) {
        if (-not $taskReply.ok) { throw $taskReply.error }
        if ($Action -eq 'Status' -or ([uint64]$taskReply.rendered_sequence -ge $taskSequence -and $taskReply.fresh)) {
            $taskReply | ConvertTo-Json -Depth 4
            return
        }
    }
    Start-Sleep -Milliseconds 50
}
throw 'The game has not acknowledged and rendered the requested camera state. No keyboard or mouse input was injected.'
