$RepoRoot = Split-Path -Parent $PSScriptRoot
$ToolchainRoot = Join-Path $RepoRoot '.toolchains'

$env:FLUTTER_ROOT = Join-Path $ToolchainRoot 'flutter'
$env:JAVA_HOME = Join-Path $ToolchainRoot 'jdk'
$env:ANDROID_HOME = Join-Path $ToolchainRoot 'android-sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:ANDROID_USER_HOME = Join-Path $ToolchainRoot 'android-user-home'
$env:PUB_CACHE = Join-Path $ToolchainRoot 'pub-cache'
$env:GRADLE_USER_HOME = Join-Path $ToolchainRoot 'gradle-home'
$env:TEMP = Join-Path $ToolchainRoot 'tmp'
$env:TMP = $env:TEMP
$env:APPDATA = Join-Path $ToolchainRoot 'appdata'
$env:LOCALAPPDATA = Join-Path $ToolchainRoot 'localappdata'
$env:FLUTTER_SUPPRESS_ANALYTICS = 'true'

$LocalPaths = @(
    (Join-Path $env:FLUTTER_ROOT 'bin'),
    (Join-Path $env:JAVA_HOME 'bin'),
    (Join-Path $env:ANDROID_HOME 'cmdline-tools\latest\bin'),
    (Join-Path $env:ANDROID_HOME 'platform-tools')
)

$env:PATH = (($LocalPaths + @($env:PATH)) -join [IO.Path]::PathSeparator)

$RequiredDirectories = @(
    $env:ANDROID_USER_HOME,
    $env:PUB_CACHE,
    $env:GRADLE_USER_HOME,
    $env:TEMP,
    $env:APPDATA,
    $env:LOCALAPPDATA
)

foreach ($Directory in $RequiredDirectories) {
    if (-not (Test-Path -LiteralPath $Directory)) {
        New-Item -ItemType Directory -Path $Directory | Out-Null
    }
}

