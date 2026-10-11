/*
 * CVE-2024-36971 vulnerability existence test
 *
 * Vulnerability description:
 * An RCU race condition in __dst_negative_advice() leads to a UAF
 * When sk->dst_cache is cleared, the wrong ordering causes a use-after-free
 *
 * Affected versions:
 * - 5.10.x < 5.10.219
 * - 5.15.x < 5.15.161
 * - 6.1.x < 6.1.94
 *
 * Patch date: 2024-08-05 (Android Security Patch)
 *
 * NOTE: this is a detection program, not a full exploit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/utsname.h>

// Parse the kernel version
int parse_kernel_version(const char *release, int *major, int *minor, int *patch)
{
    return sscanf(release, "%d.%d.%d", major, minor, patch);
}

// Check whether it is affected
int is_vulnerable(int major, int minor, int patch)
{
    // 5.10.x: affected range 5.5 - 5.10.218
    if (major == 5 && minor == 10)
    {
        return patch < 219;
    }
    // 5.15.x: affected range 5.11 - 5.15.160
    if (major == 5 && minor == 15)
    {
        return patch < 161;
    }
    // 6.1.x: affected range 5.16 - 6.1.93
    if (major == 6 && minor == 1)
    {
        return patch < 94;
    }
    // 6.6.x: affected range 6.2 - 6.6.33
    if (major == 6 && minor == 6)
    {
        return patch < 34;
    }
    // Other versions
    if (major == 5 && minor >= 5 && minor < 10)
        return 1;
    if (major == 5 && minor > 10 && minor < 15)
        return 1;
    if (major == 6 && minor > 1 && minor < 6)
        return 1;

    return 0;
}

int main()
{
    struct utsname uts;
    int major, minor, patch;

    printf("=== CVE-2024-36971 漏洞檢測 ===\n\n");

    printf("[*] 漏洞資訊:\n");
    printf("    CVE ID: CVE-2024-36971\n");
    printf("    類型: Use-After-Free (UAF)\n");
    printf("    嚴重性: High (CVSS 7.8)\n");
    printf("    元件: Linux Kernel - __dst_negative_advice()\n");
    printf("    影響: 遠端代碼執行 (RCE)\n");
    printf("    狀態: CISA KEV (已被野外利用)\n\n");

    if (uname(&uts) < 0)
    {
        perror("uname");
        return 1;
    }

    printf("[*] 系統資訊:\n");
    printf("    系統: %s\n", uts.sysname);
    printf("    核心: %s\n", uts.release);
    printf("    機器: %s\n", uts.machine);
    printf("\n");

    if (parse_kernel_version(uts.release, &major, &minor, &patch) < 3)
    {
        printf("[-] 無法解析核心版本\n");
        return 1;
    }

    printf("[*] 解析版本: %d.%d.%d\n\n", major, minor, patch);

    // Check the security patch
    printf("[*] 檢查 Android 安全補丁級別:\n");
    FILE *fp = popen("getprop ro.build.version.security_patch 2>/dev/null", "r");
    char patch_date[32] = {0};
    if (fp)
    {
        fgets(patch_date, sizeof(patch_date), fp);
        pclose(fp);
        patch_date[strcspn(patch_date, "\n")] = 0;
        printf("    安全補丁: %s\n", patch_date);

        // CVE-2024-36971 was fixed by the 2024-08-05 patch
        if (strlen(patch_date) > 0 && strcmp(patch_date, "2024-08-05") >= 0)
        {
            printf("    [+] 安全補丁 >= 2024-08-05\n\n");
        }
        else
        {
            printf("    [!] 安全補丁 < 2024-08-05 (CVE-2024-36971 修補日期)\n\n");
        }
    }

    printf("=== 漏洞評估結果 ===\n\n");

    if (is_vulnerable(major, minor, patch))
    {
        printf("[!] 核心版本 %d.%d.%d 在受影響範圍內！\n", major, minor, patch);

        if (major == 5 && minor == 10)
        {
            printf("[!] 5.10.x 需要 >= 5.10.219 才能修復\n");
            printf("[!] 當前版本 5.10.%d < 5.10.219\n", patch);
        }

        if (strlen(patch_date) > 0 && strcmp(patch_date, "2024-08-05") < 0)
        {
            printf("\n[🔴] 結論: 設備可能存在 CVE-2024-36971 漏洞！\n");
            printf("[!] 這是一個被野外積極利用的漏洞\n");
            printf("[!] 建議立即更新到 2024-08-05 或更新的安全補丁\n");
        }
        else
        {
            printf("\n[?] 安全補丁已更新，但核心版本仍舊\n");
            printf("[?] 可能透過其他方式修補\n");
        }
    }
    else
    {
        printf("[+] 核心版本 %d.%d.%d 不在受影響範圍\n", major, minor, patch);
        printf("[+] 設備應該不受此漏洞影響\n");
    }

    printf("\n=== 技術細節 ===\n");
    printf("\n漏洞位於 net/ipv4/route.c 的 __dst_negative_advice() 函數\n");
    printf("問題：清除 sk->dst_cache 時未遵循正確的 RCU 規則\n");
    printf("正確順序: 1) 清除 sk->sk_dst_cache  2) dst_release(old_dst)\n");
    printf("錯誤順序: 導致 UAF (Use-After-Free)\n");
    printf("\n利用條件:\n");
    printf("  - 需要創建 UDP socket\n");
    printf("  - 觸發路由負面建議 (negative advice)\n");
    printf("  - 競態條件窗口利用\n");

    return 0;
}
