param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$EvidencePrefix = 'cpu-applications'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $repo '.toolchains\android-sdk\platform-tools\adb.exe'
$package = 'dev.cpu_benchmark.benchmark_app'
$component = "$package/.MainActivity"
function Device([string[]]$Arguments) {
    $output = & $adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Device operation failed: $($Arguments[0])" }
    return $output
}
function Ui([string]$Name) {
    $remote = '/sdcard/rapidbench-application-ui.xml'
    $fresh = $false
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $dump = Device @('shell', 'uiautomator', 'dump', $remote)
        if ($dump -match 'dumped to:') { $fresh = $true; break }
    }
    if (-not $fresh) { throw "No fresh hierarchy for $Name" }
    $local = Join-Path $repo "artifacts\$EvidencePrefix-$Name.xml"
    Device @('pull', $remote, $local) | Out-Null
    return [xml](Get-Content -LiteralPath $local -Raw)
}
function Node($View, [string]$Pattern) {
    return $View.SelectNodes('//node') | Where-Object { $_.'content-desc' -match $Pattern } | Select-Object -First 1
}
function Bounds($Item) {
    return @([regex]::Matches($Item.bounds, '\d+') | ForEach-Object { [int]$_.Value })
}
function Tap($Item, [switch]$PreviouslyDisabled) {
    if ($null -eq $Item) { throw 'Missing UI control' }
    if (-not $PreviouslyDisabled -and ($Item.clickable -ne 'true' -or $Item.enabled -ne 'true')) { throw "Control is disabled: $($Item.'content-desc')" }
    $b = Bounds $Item
    Device @('shell', 'input', 'tap', "$([int](($b[0]+$b[2])/2))", "$([int](($b[1]+$b[3])/2))") | Out-Null
}
function Seek([string]$Name, [string]$Pattern, [bool]$Down = $true, [int]$MinimumHeight = 80) {
    for ($attempt = 0; $attempt -lt 9; $attempt++) {
        $view = Ui "$Name-$attempt"
        $item = Node $view $Pattern
        if ($null -ne $item) {
            $b = Bounds $item
            if ($b[3] - $b[1] -ge $MinimumHeight) { return $view }
        }
        $scroll = $view.SelectNodes('//node') | Where-Object { $_.scrollable -eq 'true' } | Select-Object -First 1
        if ($null -eq $scroll) { throw "No scroll area while finding $Pattern" }
        $b = Bounds $scroll
        $x = [int](($b[0]+$b[2])/2)
        $upper = [int]($b[1]+($b[3]-$b[1])*0.3)
        $lower = [int]($b[1]+($b[3]-$b[1])*0.72)
        $from = if ($Down) { $lower } else { $upper }
        $to = if ($Down) { $upper } else { $lower }
        Device @('shell', 'input', 'swipe', "$x", "$from", "$x", "$to", '300') | Out-Null
    }
    throw "Cannot find visible control: $Pattern"
}
function Screenshot([string]$Name) {
    $remote = '/sdcard/rapidbench-application-screen.png'
    Device @('shell', 'screencap', '-p', $remote) | Out-Null
    Device @('pull', $remote, (Join-Path $repo "artifacts\$EvidencePrefix-$Name.png")) | Out-Null
}
function Run-Item([string]$Name, [string]$Label, [string]$ExecutionPattern, [bool]$Down = $true) {
    $pattern = '^' + [regex]::Escape($Label) + '\n'
    $view = Seek "$Name-before" $pattern $Down 350
    Tap (Node $view $pattern)
    Start-Sleep -Seconds 5
    $view = Seek "$Name-after" $pattern $true 350
    $item = Node $view $pattern
    $description = $item.'content-desc'
    if ($description -notmatch 'COMPLETED · VERIFIED' -or $description -notmatch $ExecutionPattern -or $description -match 'Affinity fallback|Test failed') {
        throw "Invalid application result: $description"
    }
    Screenshot $Name
    Write-Output "PASS $Name : $($description -replace "`n", ' | ')"
}

Device @('shell', 'am', 'force-stop', $package) | Out-Null
Device @('shell', 'am', 'start', '-W', '-n', $component) | Out-Null
$phonePid = (Device @('shell', 'pidof', $package)).Trim()
$logPath = Join-Path $repo "artifacts\$EvidencePrefix-process.log"
$logErrors = Join-Path $repo "artifacts\$EvidencePrefix-logcat-errors.log"
$logProcess = Start-Process -FilePath $adb -WindowStyle Hidden -PassThru `
    -ArgumentList @('-s', $Serial, 'logcat', "--pid=$phonePid", '-v', 'brief', '-s', 'AndroidRuntime', 'flutter') `
    -RedirectStandardOutput $logPath -RedirectStandardError $logErrors
try {
    $view = Ui 'idle'
    $singleLabel = (Node $view '^CPU Single Thread\n').'content-desc'
    $singleCpu = [regex]::Match($singleLabel, 'CPU (\d+) •').Groups[1].Value
    $multiLabel = (Node $view '^CPU Multi Thread\n').'content-desc'
    $allWorkers = [regex]::Match($multiLabel, '(\d+) workers').Groups[1].Value
    if (-not $singleCpu -or -not $allWorkers) { throw 'CPU selection metadata missing' }
    Tap (Node $view '^APPLICATION WORKLOADS\n')
    foreach ($label in @('SORT', 'JSON RECORDS', 'IMAGE FILTER')) {
        Run-Item "single-$($label.Split(' ')[0].ToLower())" $label "CPU $singleCpu · 1 worker"
    }
    $view = Seek 'multi-selector' '^Multi$' $false
    Tap (Node $view '^Multi$')
    foreach ($label in @('SORT', 'JSON RECORDS', 'IMAGE FILTER')) {
        Run-Item "multi-$($label.Split(' ')[0].ToLower())" $label "All cores · $allWorkers independent workers"
    }

    # Completed image result and running image result have the same metadata
    # block, keeping this observed STOP position stable for the short test.
    $view = Seek 'stop-visible' '^STOP APPLICATION TEST$'
    $stopControl = Node $view '^STOP APPLICATION TEST$'
    $imageControl = Node $view '^IMAGE FILTER\n'
    Tap $imageControl
    Start-Sleep -Milliseconds 1600
    Tap $stopControl -PreviouslyDisabled
    $view = Ui 'stopped'
    if ((Node $view '^IMAGE FILTER\n').'content-desc' -notmatch 'STOPPED · PARTIAL') { throw 'Application Stop did not keep a partial result' }
    Screenshot 'stopped'
    Write-Output 'PASS: application Stop preserves a verified partial result'
    Run-Item 'restart-image' 'IMAGE FILTER' "All cores · $allWorkers independent workers"

    $view = Seek 'background-before' '^IMAGE FILTER\n' $true 350
    Tap (Node $view '^IMAGE FILTER\n')
    Start-Sleep -Milliseconds 1600
    Device @('shell', 'input', 'keyevent', '3') | Out-Null
    Start-Sleep -Seconds 1
    Device @('shell', 'am', 'start', '-W', '-n', $component) | Out-Null
    $view = Seek 'background-after' '^IMAGE FILTER\n' $true 350
    if ((Node $view '^IMAGE FILTER\n').'content-desc' -notmatch 'STOPPED · PARTIAL') { throw 'Application background stop failed' }
    Write-Output 'PASS: background cancels application test without automatic restart'

    # Changing the legacy cluster must NOT change the application's all-core mode.
    $view = Seek 'cluster-picker' '^CPU Multi Thread\n' $false 200
    Tap (Node $view '^CPU Multi Thread\n')
    $view = Ui 'cluster-options'
    $option = $view.SelectNodes('//node') | Where-Object { $_.'content-desc' -match '^G\d+\n' -and $_.clickable -eq 'true' } | Select-Object -First 1
    if ($null -eq $option) { throw 'No dynamic cluster option' }
    $group = [regex]::Match($option.'content-desc', '^G(\d+)').Groups[1].Value
    $workers = [regex]::Match($option.'content-desc', '(\d+) workers').Groups[1].Value
    if (-not $workers) { throw 'Cluster worker count missing' }
    if ([int]$workers -ge [int]$allWorkers) { throw 'Need a subset cluster to verify independence' }
    Tap $option
    $view = Ui 'legacy-subset-selected'
    if ((Node $view '^CPU Multi Thread\n').'content-desc' -notmatch "G$group") { throw 'Legacy cluster was not selected' }
    Run-Item 'auto-multi-ignores-cluster' 'JSON RECORDS' "All cores · $allWorkers independent workers"
    Write-Output "PASS: legacy G$group has $workers workers; application Multi still uses all $allWorkers"

    # Likewise select a different legacy single CPU, then check automatic Single.
    $view = Seek 'legacy-single-picker' '^CPU Single Thread\n' $false 200
    Tap (Node $view '^CPU Single Thread\n')
    $view = Ui 'legacy-single-options'
    $option = $view.SelectNodes('//node') | Where-Object {
        $_.'content-desc' -match '^CPU (\d+)\n' -and $_.clickable -eq 'true' -and
        [regex]::Match($_.'content-desc', '^CPU (\d+)').Groups[1].Value -ne $singleCpu
    } | Select-Object -First 1
    if ($null -eq $option) { throw 'No alternate legacy core for independence check' }
    $alternateCpu = [regex]::Match($option.'content-desc', '^CPU (\d+)').Groups[1].Value
    Tap $option
    $view = Ui 'legacy-alternate-cpu-selected'
    if ((Node $view '^CPU Single Thread\n').'content-desc' -notmatch "CPU $alternateCpu •") { throw 'Legacy alternate CPU was not selected' }
    $view = Seek 'auto-single-selector' '^Single$' $true
    Tap (Node $view '^Single$')
    Run-Item 'auto-single-ignores-core' 'JSON RECORDS' "CPU $singleCpu · 1 worker"
    Write-Output "PASS: legacy CPU $alternateCpu selected; application Single still targets CPU $singleCpu"

    $view = Seek 'restore-single-picker' '^CPU Single Thread\n' $false 200
    Tap (Node $view '^CPU Single Thread\n')
    $view = Seek 'restore-single-option' "^CPU $singleCpu\n" $true
    Tap (Node $view "^CPU $singleCpu\n")

    # Restore the original All-cores choice for the next manual run.
    $view = Seek 'restore-cluster-picker' '^CPU Multi Thread\n' $false 200
    Tap (Node $view '^CPU Multi Thread\n')
    $view = Ui 'restore-cluster-options'
    Tap (Node $view '^All cores\n')
    $view = Seek 'final-results' '^JSON RECORDS\n' $true 350
    Screenshot 'final-results'
    $errors = Get-Content -LiteralPath $logPath
    if ($errors -match 'FATAL EXCEPTION|Unhandled Exception|overflowed by|EXCEPTION CAUGHT') { throw 'Application process exception or layout overflow' }
    if ((Device @('shell', 'pidof', $package)).Trim() -ne $phonePid) { throw 'Application process restarted' }
    Write-Output 'PASS: new application cards completed without process crash or Flutter overflow'
} finally {
    if (-not $logProcess.HasExited) { $logProcess.Kill(); $logProcess.WaitForExit() }
}
