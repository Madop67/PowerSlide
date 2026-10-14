# Re-records the standard replay suite from power-on on Windows (about 10 minutes, in
# real time) and replaces debug/replay-suite. Linux: run the same commands by hand.
param([string]$Exe = "build-msvc-x86\Release\ctr_native.exe")

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path (Split-Path $PSScriptRoot))

$suite = @(
    @{ Name = 'attract-mode'; Frames = 9000; Script = $null },
    @{ Name = 'time-trial-crash-cove'; Frames = 2400; Script = 'tools\replays\scripts\time-trial-crash-cove.txt' },
    @{ Name = 'arcade-crash-cove'; Frames = 6000; Script = 'tools\replays\scripts\arcade-crash-cove.txt' }
)

foreach ($entry in $suite) {
    $arguments = @('--vanilla', '--record', '--from-boot', '--record-frames', "$($entry.Frames)")
    if ($entry.Script) { $arguments += @('--input-script', $entry.Script) }
    $before = Get-Date
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru -Wait -WindowStyle Minimized
    if ($process.ExitCode -ne 0) { throw "recording $($entry.Name) failed: exit $($process.ExitCode)" }
    $report = Get-ChildItem debug\reports -Recurse -Filter input.ctrreplay | Where-Object { $_.LastWriteTime -gt $before } |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $report) { throw "no report written for $($entry.Name)" }
    $target = Join-Path 'debug\replay-suite' $entry.Name
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    python tools\replays\replay_suite.py add $report.Directory.FullName $entry.Name | Select-Object -First 2
}
