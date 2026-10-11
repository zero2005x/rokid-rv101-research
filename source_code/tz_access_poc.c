/*
 * TrustZone Access PoC - attempt to connect to ssgtzd directly
 *
 * This PoC tries to connect to the TrustZone daemon using Mink Socket
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/qrtr.h>
#include <dlfcn.h>
#include <errno.h>

// Mink Socket function pointers
typedef void *(*MinkIPC_connect_t)(const char *name, void *credentials);
typedef void *(*MinkSocket_new_t)(void *ipc);
typedef int (*MinkSocket_invoke_t)(void *socket, int method, void *args);
typedef int (*MinkSocket_close_t)(void *socket);

// QSEECom function pointers
typedef int (*QSEECom_start_app_t)(void **handle, const char *path, const char *name, uint32_t size);
typedef int (*QSEECom_send_cmd_t)(void *handle, void *send_buf, uint32_t send_len, void *recv_buf, uint32_t recv_len);
typedef int (*QSEECom_shutdown_app_t)(void **handle);

int main()
{
    printf("[*] TrustZone Access PoC\n");
    printf("[*] Loading libraries...\n");
    fflush(stdout);

    // Try to load the Mink Socket library
    void *mink_lib = dlopen("libminksocket_vendor.so", RTLD_NOW);
    if (!mink_lib)
    {
        printf("[-] Failed to load libminksocket_vendor.so: %s\n", dlerror());

        // Try other paths
        mink_lib = dlopen("/vendor/lib64/libminksocket_vendor.so", RTLD_NOW);
        if (!mink_lib)
        {
            printf("[-] Also failed with full path: %s\n", dlerror());
        }
        else
        {
            printf("[+] Loaded from full path!\n");
        }
    }
    else
    {
        printf("[+] Loaded libminksocket_vendor.so\n");
    }
    fflush(stdout);

    // Try to load the QSEECom library
    void *qsee_lib = dlopen("libQSEEComAPI.so", RTLD_NOW);
    if (!qsee_lib)
    {
        printf("[-] Failed to load libQSEEComAPI.so: %s\n", dlerror());
        qsee_lib = dlopen("/vendor/lib64/libQSEEComAPI.so", RTLD_NOW);
        if (!qsee_lib)
        {
            printf("[-] Also failed with full path: %s\n", dlerror());
        }
        else
        {
            printf("[+] Loaded from full path!\n");
        }
    }
    else
    {
        printf("[+] Loaded libQSEEComAPI.so\n");
    }
    fflush(stdout);

    // Get the Mink functions
    if (mink_lib)
    {
        MinkIPC_connect_t connect_fn = (MinkIPC_connect_t)dlsym(mink_lib, "MinkIPC_connect");
        if (connect_fn)
        {
            printf("[+] Found MinkIPC_connect at %p\n", connect_fn);

            // Try to connect to ssgtzd
            printf("[*] Attempting to connect to ssgtzd...\n");
            fflush(stdout);

            void *ipc = connect_fn("ssgtzd", NULL);
            if (ipc)
            {
                printf("[+] Connected to ssgtzd! Handle: %p\n", ipc);
            }
            else
            {
                printf("[-] Failed to connect to ssgtzd\n");
            }
        }
        else
        {
            printf("[-] MinkIPC_connect not found\n");
        }
    }
    fflush(stdout);

    // Get the QSEECom functions
    if (qsee_lib)
    {
        QSEECom_start_app_t start_app = (QSEECom_start_app_t)dlsym(qsee_lib, "QSEECom_start_app");
        if (start_app)
        {
            printf("[+] Found QSEECom_start_app at %p\n", start_app);

            // Try to start a TA
            void *handle = NULL;
            printf("[*] Attempting to start a TrustZone app...\n");
            fflush(stdout);

            // Try common TA paths
            const char *ta_paths[] = {
                "/vendor/firmware_mnt/image",
                "/vendor/firmware",
                "/firmware/image",
                NULL};

            for (int i = 0; ta_paths[i]; i++)
            {
                printf("[*] Trying path: %s\n", ta_paths[i]);
                int ret = start_app(&handle, ta_paths[i], "qsee_app", 4096);
                if (ret == 0 && handle)
                {
                    printf("[+] Successfully started TA! Handle: %p\n", handle);
                    break;
                }
                else
                {
                    printf("[-] Failed (ret=%d)\n", ret);
                }
            }
        }
        else
        {
            printf("[-] QSEECom_start_app not found\n");
        }
    }
    fflush(stdout);

    // Check whether /dev/qseecom is accessible
    printf("\n[*] Checking /dev/qseecom access...\n");
    int fd = open("/dev/qseecom", O_RDWR);
    if (fd >= 0)
    {
        printf("[+] /dev/qseecom opened! fd=%d\n", fd);
        close(fd);
    }
    else
    {
        printf("[-] /dev/qseecom: %s (errno=%d)\n", strerror(errno), errno);
    }

    // Check the ssgtzd socket
    printf("\n[*] Checking ssgtzd socket...\n");
    int sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (sock >= 0)
    {
        printf("[+] QRTR socket created: %d\n", sock);
        close(sock);
    }
    else
    {
        printf("[-] QRTR socket: %s (errno=%d)\n", strerror(errno), errno);
    }

    printf("\n[*] PoC complete.\n");
    return 0;
}
