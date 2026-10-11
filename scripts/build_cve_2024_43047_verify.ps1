param(
    [switch]$Deploy,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$sdkRoot = $env:ANDROID_SDK_ROOT
if (-not $sdkRoot) {
    $sdkRoot = $env:ANDROID_HOME
}
if (-not $sdkRoot) {
    $sdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk"
}

$ndkRoot = Get-ChildItem (Join-Path $sdkRoot "ndk") -Directory -ErrorAction Stop |
    Sort-Object Name -Descending |
    Select-Object -First 1

$clang = Join-Path $ndkRoot.FullName "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd"
if (-not (Test-Path $clang)) {
    throw "aarch64-linux-android31-clang.cmd not found under $($ndkRoot.FullName)"
}

$src = Join-Path $PSScriptRoot "cve_2024_43047_verify.c"
$out = Join-Path $PSScriptRoot "cve_2024_43047_verify"

Write-Host "[build] NDK: $($ndkRoot.FullName)"
Write-Host "[build] CC : $clang"
& $clang -static -O2 -Wall -Wextra -o $out $src

if ($LASTEXITCODE -ne 0) {
    throw "compile failed: exit $LASTEXITCODE"
}

Get-Item $out | Select-Object FullName,Length,LastWriteTime

if ($Deploy -or $Run) {
    adb push $out /data/local/tmp/cve_2024_43047_verify
    adb shell chmod 755 /data/local/tmp/cve_2024_43047_verify
}

if ($Run) {
    adb shell /data/local/tmp/cve_2024_43047_verify
}