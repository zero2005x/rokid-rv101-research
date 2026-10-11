/*
 * CVE-2023-40094 測試程式
 *
 * 漏洞描述：
 * ActivityTaskManagerService.java 中的 keyguardGoingAway 方法缺少權限檢查，
 * 允許任何應用程式繞過鎖屏。
 *
 * 影響版本：Android 11, 12, 12L, 13, 14
 * 修補日期：2023-12-01
 *
 * 此測試透過 service call 來測試 IActivityTaskManager 介面
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    printf("=== CVE-2023-40094 漏洞測試 ===\n\n");

    printf("[*] 漏洞資訊:\n");
    printf("    CVE ID: CVE-2023-40094\n");
    printf("    類型: 權限繞過 (EoP)\n");
    printf("    嚴重性: High (CVSS 7.8)\n");
    printf("    影響: 繞過鎖屏\n");
    printf("    修補: 2023-12-01 安全補丁\n\n");

    printf("[*] 設備資訊:\n");
    system("echo -n '    Android: '; getprop ro.build.version.release");
    system("echo -n '    SDK: '; getprop ro.build.version.sdk");
    system("echo -n '    安全補丁: '; getprop ro.build.version.security_patch");
    printf("\n");

    // 檢查安全補丁日期
    FILE *fp = popen("getprop ro.build.version.security_patch", "r");
    char patch_date[32];
    if (fp)
    {
        fgets(patch_date, sizeof(patch_date), fp);
        pclose(fp);

        // 移除換行符
        patch_date[strcspn(patch_date, "\n")] = 0;

        // 比較日期 (簡單字串比較，因為格式是 YYYY-MM-DD)
        if (strcmp(patch_date, "2023-12-01") >= 0)
        {
            printf("[+] 安全補丁 >= 2023-12-01，漏洞應已修補\n\n");
        }
        else
        {
            printf("[!] 安全補丁 < 2023-12-01，設備可能受漏洞影響！\n\n");
        }
    }

    printf("[*] 測試 keyguardGoingAway 呼叫...\n");
    printf("    嘗試透過 service call 測試 activity_task 服務\n\n");

    // 嘗試列出 activity_task 服務的交易碼
    // keyguardGoingAway 通常是交易碼之一
    printf("[*] 檢查 activity_task 服務:\n");
    int ret = system("service check activity_task");

    if (ret == 0)
    {
        printf("\n[*] 嘗試呼叫 keyguardGoingAway (需要反射或 binder 直接調用)\n");
        printf("[*] 此簡化測試無法直接調用該方法\n");
        printf("[*] 完整測試需要 Android APK 使用反射調用\n\n");

        printf("=== 測試結論 ===\n");
        printf("[*] 設備安全補丁: %s\n", patch_date);
        if (strcmp(patch_date, "2023-12-01") >= 0)
        {
            printf("[+] 設備已包含 CVE-2023-40094 修補\n");
            printf("[+] keyguardGoingAway 現在需要 CONTROL_KEYGUARD 權限\n");
            printf("[-] 漏洞無法利用\n");
        }
        else
        {
            printf("[!] 設備可能存在漏洞！\n");
            printf("[!] 需要進一步使用 APK 進行測試\n");
        }
    }
    else
    {
        printf("[-] 無法存取 activity_task 服務\n");
    }

    return 0;
}
