param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [string]$EvidencePrefix = 'beta5-debug'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $repo '.toolchains\android-sdk\platform-tools\adb.exe'
$package = 'dev.cpu_benchmark.benchmark_app'
$component = "$package/.MainActivity"

function Invoke-Device([string[]]$Arguments) {
    $output = & $adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Device command failed: $($Arguments[0])" }
    return $output
}
function Read-Ui([string]$Name) {
    $remote = '/sdcard/rapidbench-beta5-ui.xml'
    # A continuously updating screen may never reach accessibility idle. Some
    # Android versions return exit code zero without replacing the old XML.
    $dumped = $false
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $output = Invoke-Device @('shell', 'uiautomator', 'dump', $remote)
        if ($output -match 'dumped to:') { $dumped = $true; break }
    }
    if (-not $dumped) { throw "No fresh UI hierarchy for $Name" }
    $local = Join-Path $repo "artifacts\$EvidencePrefix-$Name.xml"
    Invoke-Device @('pull', $remote, $local) | Out-Null
    return [xml](Get-Content -LiteralPath $local -Raw)
}
function Find-Node($Ui, [string]$Label) {
    $node = $Ui.SelectNodes('//node') | Where-Object { $_.'content-desc' -eq $Label -or $_.text -eq $Label } | Select-Object -First 1
    if ($null -eq $node) { throw "UI control missing: $Label" }
    return $node
}
function Tap-Node($Node, [switch]$PreviouslyDisabled) {
    if (-not $PreviouslyDisabled -and ($Node.enabled -ne 'true' -or $Node.clickable -ne 'true')) { throw 'UI control is not actionable' }
    $numbers = [regex]::Matches($Node.bounds, '\d+') | ForEach-Object { [int]$_.Value }
    $x = [int](($numbers[0] + $numbers[2]) / 2)
    $y = [int](($numbers[1] + $numbers[3]) / 2)
    Invoke-Device @('shell', 'input', 'tap', "$x", "$y") | Out-Null
}
function Navigate($Ui, [string]$Label) { Tap-Node (Find-Node $Ui "$Label`n$Label") }
function Assert-Enabled($Ui, [string]$Label, [bool]$Expected) {
    $actual = (Find-Node $Ui $Label).enabled -eq 'true'
    if ($actual -ne $Expected) { throw "$Label enabled=$actual, expected=$Expected" }
}
function Read-ControlUi([string]$Name, [string]$Label) {
    for ($attempt = 0; $attempt -lt 4; $attempt++) {
        $ui = Read-Ui $Name
        $found = $ui.SelectNodes('//node') | Where-Object { $_.'content-desc' -eq $Label }
        if ($found) { return $ui }
        $scroll = $ui.SelectNodes('//node') | Where-Object { $_.scrollable -eq 'true' } | Select-Object -First 1
        if ($null -eq $scroll) { throw "No scroll area for $Label" }
        $bounds = [regex]::Matches($scroll.bounds, '\d+') | ForEach-Object { [int]$_.Value }
        $x = [int](($bounds[0] + $bounds[2]) / 2)
        $fromY = [int]($bounds[1] + ($bounds[3] - $bounds[1]) * 0.8)
        $toY = [int]($bounds[1] + ($bounds[3] - $bounds[1]) * 0.3)
        Invoke-Device @('shell', 'input', 'swipe', "$x", "$fromY", "$x", "$toY", '300') | Out-Null
    }
    throw "Off-screen control not found: $Label"
}
function Wait-Ready([string]$Name, [string]$Button, [int]$TimeoutSeconds = 90) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        try { $ui = Read-ControlUi $Name $Button }
        catch {
            if ($_.Exception.Message -notlike 'No fresh UI hierarchy*') { throw }
            # Long Storage/GPU sequences can keep refreshing throughout a dump
            # retry window. Keep waiting, bounded by this stage's deadline.
            continue
        }
        if ((Find-Node $ui $Button).enabled -eq 'true') { return $ui }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$Name did not finish before timeout"
}

Invoke-Device @('shell', 'am', 'force-stop', $package) | Out-Null
Invoke-Device @('shell', 'am', 'start', '-W', '-n', $component) | Out-Null
$phonePid = (Invoke-Device @('shell', 'pidof', $package)).Trim()
$logPath = Join-Path $repo "artifacts\$EvidencePrefix-app-process.log"
$logErrors = Join-Path $repo "artifacts\$EvidencePrefix-logcat-errors.log"
# Small device logcat ring buffers can evict early results before Bench All
# finishes. Stream only this App process's relevant tags from launch onward.
$logProcess = Start-Process -FilePath $adb -WindowStyle Hidden -PassThru `
    -ArgumentList @('-s', $Serial, 'logcat', "--pid=$phonePid", '-v', 'brief', '-s', 'RapidBenchStorage', 'AndroidRuntime', 'flutter') `
    -RedirectStandardOutput $logPath -RedirectStandardError $logErrors
try {
$ui = Read-Ui 'cpu-idle'
Tap-Node (Find-Node $ui 'BENCH CPU')
Navigate $ui 'MEMORY'
$ui = Read-Ui 'cpu-locks-memory'
Assert-Enabled $ui 'BENCH MEMORY' $false
$copy = $ui.SelectNodes('//node') | Where-Object { $_.'content-desc' -like 'COPY*GB/s' } | Select-Object -First 1
if ($null -eq $copy -or $copy.clickable -ne 'false') { throw 'Memory single card was not blocked' }
Navigate $ui 'STORAGE'
$ui = Read-Ui 'cpu-locks-storage'
Assert-Enabled $ui 'BENCH ALL' $false
Navigate $ui 'GPU'
$ui = Read-Ui 'cpu-locks-gpu'
$fp32 = $ui.SelectNodes('//node') | Where-Object { $_.'content-desc' -like 'FP32 COMPUTE*' } | Select-Object -First 1
if ($null -eq $fp32 -or $fp32.clickable -ne 'false') { throw 'GPU single card was not blocked' }
'PASS: CPU excludes Memory, Storage, GPU while navigation remains available'

# UI dumps across four pages can outlast a CPU sequence. Start a fresh run for
# the lifecycle check instead of assuming the navigation run is still active.
Navigate $ui 'CPU'
$ui = Wait-Ready 'cpu-before-background' 'BENCH CPU' 35
Tap-Node (Find-Node $ui 'BENCH CPU')
Start-Sleep -Seconds 1
Invoke-Device @('shell', 'input', 'keyevent', '3') | Out-Null
Start-Sleep -Seconds 1
$activity = Invoke-Device @('shell', 'dumpsys', 'activity', 'activities')
$resumed = $activity | Select-String 'topResumedActivity|mResumedActivity'
if (-not $resumed -or ($resumed -match [regex]::Escape($package))) {
    throw 'App did not leave the foreground for the lifecycle check'
}
Invoke-Device @('shell', 'am', 'start', '-W', '-n', $component) | Out-Null
$ui = Wait-Ready 'cpu-background-cancelled' 'BENCH CPU' 20
if ($ui.OuterXml -notlike '*Cancelled*') { throw 'CPU did not retain a cancelled result after backgrounding' }
'PASS: background requests stop and releases ownership after completion'

Navigate $ui 'MEMORY'
$ui = Read-Ui 'memory-idle'
Tap-Node (Find-Node $ui 'BENCH MEMORY')
# STOP stays at the idle screen's observed bounds and becomes enabled on start.
# Do not wait for accessibility idle while the short memory run is refreshing.
Start-Sleep -Seconds 1
Tap-Node (Find-Node $ui 'STOP') -PreviouslyDisabled
$ui = Wait-Ready 'memory-stopped' 'BENCH MEMORY' 20
if ($ui.OuterXml -notlike '*Stopped*') { throw 'Memory Stop did not produce stopped state' }
Tap-Node (Find-Node $ui 'BENCH MEMORY')
$ui = Wait-Ready 'memory-completed' 'BENCH MEMORY' 35
if ($ui.OuterXml -notlike '*Completed*') { throw 'Memory sequence did not complete' }
'PASS: Memory stop, restart, and full Read/Write/Copy sequence'

Navigate $ui 'STORAGE'
$ui = Read-Ui 'storage-idle'
Tap-Node (Find-Node $ui 'BENCH ALL')
$ui = Wait-Ready 'storage-completed' 'BENCH ALL' 90
if ($ui.OuterXml -like '*UNAVAILABLE*' -or $ui.OuterXml -notlike '*Completed*') { throw 'Storage full run contains an unavailable result or did not complete' }
$storageLog = Get-Content -LiteralPath $logPath
foreach ($id in 1..11) {
    if (-not ($storageLog -match "result test=$id valid=1 stopped=0 error=0 ")) { throw "Storage result $id missing from app-process log" }
}
$storageLog
'PASS: all 11 Storage tests completed inside the App'

Navigate $ui 'GPU'
$ui = Read-ControlUi 'gpu-idle' 'BENCH GPU'
Tap-Node (Find-Node $ui 'BENCH GPU')
$ui = Wait-Ready 'gpu-completed' 'BENCH GPU' 90
if ($ui.OuterXml -notlike '*Completed*' -or $ui.OuterXml -like '*Native GPU test failed*') { throw 'GPU sequence did not complete' }
'PASS: GPU complete sequence'

$crashes = Get-Content -LiteralPath $logPath
if ($crashes -match 'FATAL EXCEPTION|Unhandled Exception|overflowed by|EXCEPTION CAUGHT') { throw 'App process contains an exception or layout overflow' }
if ($logProcess.HasExited) { throw 'App log capture exited before validation finished' }
if ((Invoke-Device @('shell', 'pidof', $package)).Trim() -ne $phonePid) { throw 'App process changed during validation' }
'PASS: no app-process crash, unhandled exception, or Flutter overflow'
} finally {
    if (-not $logProcess.HasExited) { $logProcess.Kill(); $logProcess.WaitForExit() }
}
