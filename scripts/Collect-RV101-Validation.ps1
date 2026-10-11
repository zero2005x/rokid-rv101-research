#requires -Version 5.1

<#
.SYNOPSIS
Collects read-only validation evidence from a Rokid Glasses RV101.

.DESCRIPTION
Every ADB operation explicitly uses the selected USB serial, so a simultaneous
wireless ADB connection cannot cause "more than one device/emulator" failures.

The script does not install apps, change settings, enable Wi-Fi Aware, unlock
the bootloader, run exploits, or write files to the glasses. It writes evidence
only to the Windows computer, records every command's exit code and file size,
pulls the live SELinux policy when readable, and creates a ZIP for review.

.EXAMPLE
Set-ExecutionPolicy -Scope Process Bypass
.\Collect-RV101-Validation.ps1

.EXAMPLE
.\Collect-RV101-Validation.ps1 -Serial "<眼鏡序號>" -NoZip   # 或設環境變數 SERIAL
#>

[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    # 由參數或環境變數 SERIAL 提供（不寫死裝置序號）
    [string]$Serial = $env:SERIAL,

    [Parameter()]
    [string]$OutputRoot = "",

    [Parameter()]
    [switch]$SkipSePolicy,

    [Parameter()]
    [switch]$NoZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$CollectorVersion = "1.1.0"

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Results = New-Object System.Collections.Generic.List[object]

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [AllowNull()][object[]]$Content
    )

    $lines = @()
    if ($null -ne $Content) {
        $lines = @($Content | ForEach-Object { [string]$_ })
    }

    $text = $lines -join [Environment]::NewLine
    if ($text.Length -gt 0) {
        $text += [Environment]::NewLine
    }

    [System.IO.File]::WriteAllText($Path, $text, $script:Utf8NoBom)
}

function Add-Result {
    param(
        [string]$Name,
        [string]$Kind,
        [string]$Command,
        [string]$OutputFile,
        [int]$ExitCode,
        [AllowNull()][Nullable[int]]$RemoteLastExit = $null
    )

    $bytes = 0
    if (Test-Path -LiteralPath $OutputFile) {
        $bytes = (Get-Item -LiteralPath $OutputFile).Length
    }

    $status = if ($ExitCode -ne 0) {
        "FAILED"
    }
    elseif (($null -ne $RemoteLastExit) -and ($RemoteLastExit -ne 0)) {
        "REMOTE_NONZERO"
    }
    elseif ($bytes -eq 0) {
        "EMPTY_SUCCESS"
    }
    else {
        "OK"
    }

    $script:Results.Add([pscustomobject]@{
        Name       = $Name
        Kind       = $Kind
        ExitCode   = $ExitCode
        RemoteLastExit = $RemoteLastExit
        Status     = $status
        Bytes      = $bytes
        OutputFile = [System.IO.Path]::GetFileName($OutputFile)
        Command    = $Command
    })

    $color = switch ($status) {
        "OK"            { "Green" }
        "REMOTE_NONZERO" { "Yellow" }
        "EMPTY_SUCCESS" { "Yellow" }
        default          { "Red" }
    }
    Write-Host ("[{0}] {1} ({2} bytes, exit {3})" -f $status, $Name, $bytes, $ExitCode) -ForegroundColor $color
}

function Invoke-AdbShellCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$RemoteCommand
    )

    $outputPath = Join-Path $script:RunDirectory ($Name + ".txt")
    $wrappedCommand = $RemoteCommand + '; __collector_last_exit=$?; echo "__collector_last_exit=$__collector_last_exit"; exit 0'

    # Send the remote script through stdin. This avoids Windows/adb.exe stripping
    # the quotes around regex alternation such as "adsprpc|fastrpc|kgsl".
    # -T disables PTY allocation so stdin remains an exact, non-interactive script.
    $savedErrorActionPreference = $ErrorActionPreference
    $output = @()
    $exitCode = 1
    try {
        # Windows PowerShell 5.1 can promote native stderr to NativeCommandError
        # when ErrorActionPreference is Stop. Continue keeps expected Android
        # permission/path errors in the evidence file instead of aborting the run.
        $ErrorActionPreference = "Continue"
        $output = @($wrappedCommand | & $script:AdbExe -s $script:Serial shell -T 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }

    Write-Utf8File -Path $outputPath -Content $output

    $remoteLastExit = $null
    foreach ($line in $output) {
        if (([string]$line) -match '^__collector_last_exit=([0-9]+)$') {
            $remoteLastExit = [int]$Matches[1]
        }
    }

    Add-Result -Name $Name -Kind "adb-shell" -Command $RemoteCommand -OutputFile $outputPath -ExitCode $exitCode -RemoteLastExit $remoteLastExit
}

function Invoke-AdbHostCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $outputPath = Join-Path $script:RunDirectory ($Name + ".txt")
    $savedErrorActionPreference = $ErrorActionPreference
    $output = @()
    $exitCode = 1
    try {
        $ErrorActionPreference = "Continue"
        $output = @(& $script:AdbExe @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    Write-Utf8File -Path $outputPath -Content $output
    Add-Result -Name $Name -Kind "adb-host" -Command ("adb " + ($Arguments -join " ")) -OutputFile $outputPath -ExitCode $exitCode
}

function Invoke-AdbPullCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$RemotePath,
        [Parameter(Mandatory = $true)][string]$LocalPath
    )

    $logPath = Join-Path $script:RunDirectory ($Name + "_pull_log.txt")
    $savedErrorActionPreference = $ErrorActionPreference
    $output = @()
    $exitCode = 1
    try {
        $ErrorActionPreference = "Continue"
        $output = @(& $script:AdbExe -s $script:Serial pull $RemotePath $LocalPath 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    Write-Utf8File -Path $logPath -Content $output

    $resultPath = if (Test-Path -LiteralPath $LocalPath) { $LocalPath } else { $logPath }
    Add-Result -Name $Name -Kind "adb-pull" -Command ("adb -s {0} pull {1}" -f $script:Serial, $RemotePath) -OutputFile $resultPath -ExitCode $exitCode
}

try {
    $adbCommand = Get-Command adb -ErrorAction Stop
    $AdbExe = $adbCommand.Source

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
        $baseDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
            (Get-Location).Path
        }
        else {
            $PSScriptRoot
        }
        $OutputRoot = Join-Path $baseDirectory "RV101_Validation"
    }

    $runStamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $RunDirectory = Join-Path $OutputRoot ("RV101_{0}_{1}" -f $Serial, $runStamp)
    New-Item -ItemType Directory -Path $RunDirectory -Force | Out-Null

    if (-not [string]::IsNullOrWhiteSpace($PSCommandPath) -and (Test-Path -LiteralPath $PSCommandPath)) {
        Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $RunDirectory "collector_script.ps1") -Force
    }

    Write-Host "RV101 read-only validation collection" -ForegroundColor Cyan
    Write-Host ("Collector      : {0}" -f $CollectorVersion)
    Write-Host ("ADB executable : {0}" -f $AdbExe)
    Write-Host ("Target serial  : {0}" -f $Serial)
    Write-Host ("Output folder  : {0}" -f $RunDirectory)

    $hostContextPath = Join-Path $RunDirectory "00_host_context.txt"
    $adbVersion = @(& $AdbExe version 2>&1)
    $adbVersionExit = $LASTEXITCODE
    Write-Utf8File -Path $hostContextPath -Content @(
        "collector_version=$CollectorVersion",
        "host_time=$((Get-Date).ToString('o'))",
        "powershell_version=$($PSVersionTable.PSVersion)",
        "powershell_edition=$(if ($PSVersionTable.ContainsKey('PSEdition')) { $PSVersionTable.PSEdition } else { 'Desktop' })",
        "host_os=$([System.Environment]::OSVersion.VersionString)",
        "adb_path=$AdbExe",
        $adbVersion
    )
    Add-Result -Name "00_host_context" -Kind "host" -Command "adb version" -OutputFile $hostContextPath -ExitCode $adbVersionExit

    # Preserve the full host device list, but use the selected USB serial below.
    $deviceList = @(& $AdbExe devices -l 2>&1)
    $deviceListExit = $LASTEXITCODE
    $deviceListPath = Join-Path $RunDirectory "00_adb_devices.txt"
    Write-Utf8File -Path $deviceListPath -Content $deviceList
    Add-Result -Name "00_adb_devices" -Kind "adb-host" -Command "adb devices -l" -OutputFile $deviceListPath -ExitCode $deviceListExit

    $escapedSerial = [regex]::Escape($Serial)
    $targetLine = @($deviceList | Where-Object { ([string]$_) -match ('^{0}\s+device(?:\s|$)' -f $escapedSerial) })
    if ($targetLine.Count -eq 0) {
        throw "The selected serial '$Serial' is not listed in the 'device' state. Check USB debugging and authorization."
    }

    $state = @(& $AdbExe -s $Serial get-state 2>&1)
    if (($LASTEXITCODE -ne 0) -or (($state -join "").Trim() -ne "device")) {
        throw "ADB target '$Serial' did not return the 'device' state: $($state -join ' ')"
    }

    # Device time, identity, build and security baseline.
    Invoke-AdbShellCapture "01_context_time" 'echo "device_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)"; echo "uptime=$(cat /proc/uptime 2>/dev/null)"; id'

    Invoke-AdbShellCapture "02_identity_build" 'for p in ro.product.manufacturer ro.product.brand ro.product.model ro.product.name ro.product.device ro.build.version.release ro.build.version.sdk ro.build.id ro.build.display.id ro.build.version.incremental ro.build.fingerprint ro.build.type ro.build.tags ro.board.platform ro.hardware ro.soc.manufacturer ro.soc.model ro.boot.hardware; do echo "$p=$(getprop $p)"; done'

    Invoke-AdbShellCapture "03_security_baseline" 'getenforce 2>&1; for p in ro.build.version.security_patch ro.vendor.build.security_patch ro.system.build.security_patch ro.keymaster.3.security_patch ro.boot.verifiedbootstate ro.boot.vbmeta.device_state ro.boot.flash.locked ro.boot.veritymode ro.boot.avb_version ro.boot.selinux ro.debuggable ro.adb.secure ro.secure persist.sys.usb.config sys.usb.config sys.usb.state ro.boot.slot_suffix ro.build.ab_update ro.virtual_ab.enabled; do echo "$p=$(getprop $p)"; done; echo "selinux_enforce=$(cat /sys/fs/selinux/enforce 2>/dev/null)"'

    Invoke-AdbShellCapture "04_kernel_runtime" 'uname -a; cat /proc/version 2>&1; echo "osrelease=$(cat /proc/sys/kernel/osrelease 2>/dev/null)"; echo "randomize_va_space=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)"; echo "cmdline:"; cat /proc/cmdline 2>&1'

    # Loaded modules and relevant kernel configuration.
    Invoke-AdbShellCapture "05_lsmod_full" 'lsmod 2>&1'
    Invoke-AdbShellCapture "06_lsmod_relevant" 'lsmod 2>&1 | grep -iE "adsprpc|fastrpc|kgsl|msm_drm|disp|cnss|icnss|kiwi|wlan|msm_eva|eva|camera|smcinvoke|qseecom|hwkm|eud|rdbg|dcc|qdss|diag|f2fs"'
    Invoke-AdbShellCapture "07_kernel_config_relevant" 'if [ -r /proc/config.gz ]; then zcat /proc/config.gz 2>&1 | grep -iE "CONFIG_(F2FS|CGROUP|CGROUPS|CGROUP_V1|CGROUP_V2|USER_NS|SECCOMP|KALLSYMS|RANDOMIZE|ARM64_PTR_AUTH|CFI|SHADOW_CALL_STACK|ARM64_MTE|KGSL|EVA|CNSS|ADSPRPC|FASTRPC|QSEECOM|SMCINVOKE)"; elif [ -r /boot/config-$(uname -r) ]; then cat /boot/config-$(uname -r) | grep -iE "CONFIG_(F2FS|CGROUP|USER_NS|SECCOMP|KALLSYMS|RANDOMIZE|CFI|MTE|KGSL|EVA|CNSS|ADSPRPC|QSEECOM|SMCINVOKE)"; else echo KERNEL_CONFIG_UNAVAILABLE; fi'

    # Device nodes: DAC permissions and SELinux labels.
    Invoke-AdbShellCapture "08_devnodes_gpu_display" 'ls -laZ /dev/kgsl* /dev/dri/* 2>&1; echo "-- kgsl sysfs --"; ls -laZ /sys/class/kgsl /sys/class/kgsl/kgsl-3d0 2>&1; readlink -f /sys/class/kgsl/kgsl-3d0/device/driver 2>&1'
    Invoke-AdbShellCapture "09_devnodes_fastrpc" 'ls -laZ /dev/adsprpc* /dev/cdsprpc* /dev/fastrpc* 2>&1'
    Invoke-AdbShellCapture "10_devnodes_tee_strongbox" 'ls -laZ /dev/qseecom /dev/smcinvoke /dev/hwkm* 2>&1'
    Invoke-AdbShellCapture "11_devnodes_eva_camera" 'ls -laZ /dev/msm_eva* /dev/eva* /dev/media* /dev/video* /dev/camera* 2>&1'
    Invoke-AdbShellCapture "12_devnodes_wlan" 'ls -laZ /dev/wlan /dev/cnss* /dev/wifi* 2>&1; echo "-- net classes --"; ls -laZ /sys/class/net 2>&1'
    Invoke-AdbShellCapture "13_devnodes_diag_qdss" 'ls -laZ /dev/diag /dev/qdss* /dev/eud* /dev/dcc* 2>&1'

    # Wi-Fi Aware/NAN. This is passive: the script does not call attach or enable it.
    Invoke-AdbShellCapture "14_wifiaware_full" 'dumpsys wifiaware 2>&1'
    Invoke-AdbShellCapture "15_wifiaware_summary" 'dumpsys wifiaware 2>&1 | grep -iE "mUsageEnabled|mWifiNanIface|mReferenceCount|mInterfaces|curState|maxConcurrentAwareClusters|maxPublishes|maxSubscribes|attached|client"; echo "-- properties --"; getprop | grep -i nan; echo "-- declared feature --"; pm list features | grep -iE "wifi.aware|wifi.direct|wifi"'
    Invoke-AdbShellCapture "16_wlan_sysfs_modules" 'echo "-- interfaces --"; ls -l /sys/class/net 2>&1; echo "-- modules --"; ls -l /sys/module 2>&1 | grep -iE "cnss|wlan|cld|icnss|kiwi"; echo "-- firmware and driver links --"; for n in /sys/class/net/*; do echo "[$n]"; readlink -f "$n/device/driver" 2>/dev/null; readlink -f "$n/device/firmware_node" 2>/dev/null; done'

    # SELinux runtime state and process/service attack surfaces.
    Invoke-AdbShellCapture "17_selinux_runtime" 'getenforce 2>&1; id; ls -laZ /sys/fs/selinux/policy 2>&1; echo "policy_bytes=$(wc -c < /sys/fs/selinux/policy 2>/dev/null)"; echo "policyvers=$(cat /sys/fs/selinux/policyvers 2>/dev/null)"; echo "mls=$(cat /sys/fs/selinux/mls 2>/dev/null)"; sha256sum /sys/fs/selinux/policy 2>&1'
    Invoke-AdbShellCapture "18_process_domains_full" 'ps -A -Z 2>&1'
    Invoke-AdbShellCapture "19_process_domains_relevant" 'ps -A -Z 2>&1 | grep -iE "kgsl|gpu|adsprpc|fastrpc|dsp|qsee|tee|keymaster|keymint|gatekeeper|secureprocessor|camera|eva|cvp|scve|qvr|sxr|wlan|wifi|diag|qdss"'
    Invoke-AdbShellCapture "20_android_services" 'service list 2>&1'
    Invoke-AdbShellCapture "21_hal_manifest" 'lshal 2>&1'
    Invoke-AdbShellCapture "22_security_xr_services" 'echo "-- service list --"; service list 2>&1 | grep -iE "keymaster|keymint|gatekeeper|secure|qsee|tee|dsp|fast|camera|eva|cvp|scve|qvr|sxr|vr|wifi|aware"; echo "-- lshal --"; lshal 2>&1 | grep -iE "keymaster|keymint|gatekeeper|secure|qsee|tee|dsp|fast|camera|eva|cvp|scve|qvr|sxr|vr|wifi|aware"; echo "-- vendor hw executables --"; ls -laZ /vendor/bin/hw 2>&1 | grep -iE "key|gate|secure|qsee|tee|dsp|camera|eva|cvp|scve|qvr|sxr|vr|wifi"'

    # SoC and CPU topology, including offline cores and maximum frequencies.
    Invoke-AdbShellCapture "23_soc_properties" 'getprop | grep -iE "soc|platform|board|hardware|chipset|product"'
    Invoke-AdbShellCapture "24_soc0" 'for f in soc_id family machine hw_platform revision serial_number; do echo "[$f]"; cat "/sys/devices/soc0/$f" 2>&1; done; echo "-- soc devices --"; ls -la /sys/devices/soc0 2>&1'
    Invoke-AdbShellCapture "25_cpu_topology" 'for f in possible present online offline; do echo "$f=$(cat /sys/devices/system/cpu/$f 2>/dev/null)"; done; for c in /sys/devices/system/cpu/cpu[0-9]*; do echo "[$c]"; cat "$c/topology/core_id" 2>/dev/null; cat "$c/topology/physical_package_id" 2>/dev/null; cat "$c/cpufreq/cpuinfo_max_freq" 2>/dev/null; done'
    Invoke-AdbShellCapture "26_cpuinfo" 'cat /proc/cpuinfo 2>&1'

    # Kernel CVE applicability evidence: filesystems, cgroups and AF_UNIX.
    Invoke-AdbShellCapture "27_filesystems_mounts" 'echo "-- filesystems --"; cat /proc/filesystems 2>&1; echo "-- mounts --"; cat /proc/mounts 2>&1; echo "-- df --"; df -T 2>&1'
    Invoke-AdbShellCapture "28_cgroup_runtime" 'echo "-- /proc/cgroups --"; cat /proc/cgroups 2>&1; echo "-- self cgroup --"; cat /proc/self/cgroup 2>&1; echo "-- cgroup mounts --"; mount 2>&1 | grep -i cgroup; echo "-- cgroup root --"; ls -laZ /sys/fs/cgroup 2>&1; echo "unprivileged_userns_clone=$(cat /proc/sys/kernel/unprivileged_userns_clone 2>/dev/null)"'
    Invoke-AdbShellCapture "29_f2fs_runtime" 'echo "-- support --"; cat /proc/filesystems 2>&1 | grep -i f2fs; echo "-- mounts --"; mount 2>&1 | grep -i f2fs; echo "-- sysfs --"; ls -laZ /sys/fs/f2fs 2>&1; echo "-- block filesystems --"; for b in /sys/class/block/*; do t=$(cat "$b/uevent" 2>/dev/null | grep -E "DEVNAME|PARTNAME"); [ -n "$t" ] && echo "[$b] $t"; done'
    Invoke-AdbShellCapture "30_af_unix_runtime" 'echo "kernel=$(uname -r)"; echo "-- protocols --"; cat /proc/net/protocols 2>&1 | grep -i UNIX; echo "-- unix sockets --"; cat /proc/net/unix 2>&1'

    # OTA/partition properties and package-level mitigations.
    Invoke-AdbShellCapture "31_partition_ota" 'for p in ro.boot.slot_suffix ro.boot.slot ro.boot.dynamic_partitions ro.build.ab_update ro.virtual_ab.enabled ro.virtual_ab.retrofit ro.boot.super_partition ro.boot.verifiedbootstate ro.boot.vbmeta.device_state; do echo "$p=$(getprop $p)"; done; echo "-- by-name --"; ls -laZ /dev/block/by-name 2>&1; echo "-- mapper --"; ls -laZ /dev/block/mapper 2>&1'
    Invoke-AdbShellCapture "32_gms_play_protect" 'pm list packages 2>&1 | grep -iE "com.google.android.gms|com.android.vending|playprotect|com.google.android.gsf"; echo "-- matching services --"; service list 2>&1 | grep -iE "gms|vending|protect"'
    Invoke-AdbShellCapture "33_declared_features" 'pm list features 2>&1 | sort'
    Invoke-AdbShellCapture "34_usb_functions" 'for p in persist.sys.usb.config sys.usb.config sys.usb.state vendor.usb.config persist.vendor.usb.config; do echo "$p=$(getprop $p)"; done; echo "-- configfs gadget --"; ls -laZ /config/usb_gadget 2>&1; find /config/usb_gadget -maxdepth 4 -type l -o -type d 2>&1'

    if (-not $SkipSePolicy) {
        $policyPath = Join-Path $RunDirectory "sepolicy.bin"
        Invoke-AdbPullCapture -Name "90_sepolicy" -RemotePath "/sys/fs/selinux/policy" -LocalPath $policyPath
        if ((Test-Path -LiteralPath $policyPath) -and ((Get-Item -LiteralPath $policyPath).Length -gt 0)) {
            $policyHash = Get-FileHash -Algorithm SHA256 -LiteralPath $policyPath
            Write-Utf8File -Path (Join-Path $RunDirectory "90_sepolicy_sha256.txt") -Content @(
                "Algorithm=$($policyHash.Algorithm)",
                "Hash=$($policyHash.Hash)",
                "Bytes=$((Get-Item -LiteralPath $policyPath).Length)"
            )
        }
    }

    # Export command outcomes before creating the ZIP.
    $manifestPath = Join-Path $RunDirectory "command_manifest.csv"
    $Results | Export-Csv -LiteralPath $manifestPath -NoTypeInformation -Encoding UTF8

    $failedCount = @($Results | Where-Object { $_.Status -eq "FAILED" }).Count
    $remoteNonzeroCount = @($Results | Where-Object { $_.Status -eq "REMOTE_NONZERO" }).Count
    $emptyCount = @($Results | Where-Object { $_.Status -eq "EMPTY_SUCCESS" }).Count
    $okCount = @($Results | Where-Object { $_.Status -eq "OK" }).Count

    $summaryPath = Join-Path $RunDirectory "README_RESULTS.txt"
    Write-Utf8File -Path $summaryPath -Content @(
        "Rokid Glasses RV101 validation evidence",
        "Host collection time: $((Get-Date).ToString('o'))",
        "Selected ADB serial: $Serial",
        "Output directory: $RunDirectory",
        "Successful captures: $okCount",
        "Remote commands whose final subcommand returned nonzero: $remoteNonzeroCount",
        "Empty but exit-0 captures: $emptyCount",
        "Failed captures: $failedCount",
        "",
        "Review command_manifest.csv before interpreting an empty file.",
        "ExitCode is the ADB transport/shell result. RemoteLastExit is the final device-side subcommand result.",
        "REMOTE_NONZERO often means an optional path, command, or grep match was absent; inspect the text file before drawing a conclusion.",
        "A failed ADB transport must not be treated as evidence that a feature is absent.",
        "This collection is passive and does not prove that a listed CVE is exploitable or patched.",
        "WifiAwareManager.attach() from an ordinary third-party app is not performed by this script."
    )

    # Hash every completed evidence file for integrity and traceability.
    $hashRows = Get-ChildItem -LiteralPath $RunDirectory -File | ForEach-Object {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
        [pscustomobject]@{
            File   = $_.Name
            Bytes  = $_.Length
            SHA256 = $hash.Hash
        }
    }
    $hashRows | Export-Csv -LiteralPath (Join-Path $RunDirectory "file_hashes.csv") -NoTypeInformation -Encoding UTF8

    $zipPath = $null
    if (-not $NoZip) {
        $zipPath = $RunDirectory + ".zip"
        Compress-Archive -Path (Join-Path $RunDirectory "*") -DestinationPath $zipPath -Force
    }

    Write-Host "" 
    Write-Host "Collection complete." -ForegroundColor Cyan
    Write-Host ("OK: {0}; remote-nonzero: {1}; empty-success: {2}; failed: {3}" -f $okCount, $remoteNonzeroCount, $emptyCount, $failedCount)
    Write-Host ("Evidence folder: {0}" -f $RunDirectory) -ForegroundColor Yellow
    if ($null -ne $zipPath) {
        Write-Host ("Upload this ZIP: {0}" -f $zipPath) -ForegroundColor Yellow
    }
    else {
        Write-Host "ZIP creation was disabled with -NoZip."
    }

    if ($failedCount -gt 0) {
        Write-Warning "Some commands failed. This can be normal on a non-root production build; command_manifest.csv records each failure."
    }
}
catch {
    Write-Error $_
    exit 1
}
