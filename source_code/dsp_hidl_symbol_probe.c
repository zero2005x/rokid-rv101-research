/*
 * CVE-2024-43047 / CVE-2024-49848
 * dsp_hidl_symbol_probe.c - non-invasive HIDL symbol probe for DSP service libs.
 *
 * This tool only dlopens libraries and checks symbol availability. It does not
 * call IDspService::getNewDSPSession, IDspManager::openSession, or any ioctl.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

struct symbol_check
{
    const char *label;
    const char *name;
};

static void check_symbols(void *handle, const char *libname, const struct symbol_check *symbols)
{
    printf("\n[*] Symbols in %s\n", libname);
    for (int i = 0; symbols[i].name != NULL; i++)
    {
        dlerror();
        void *addr = dlsym(handle, symbols[i].name);
        const char *err = dlerror();
        if (addr && !err)
        {
            printf("  [+] %-42s %p\n", symbols[i].label, addr);
        }
        else
        {
            printf("  [-] %-42s missing\n", symbols[i].label);
        }
    }
}

static void open_and_check(const char *libname, const struct symbol_check *symbols)
{
    printf("\n=== dlopen %s ===\n", libname);
    void *handle = dlopen(libname, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        printf("[-] dlopen failed: %s\n", dlerror());
        return;
    }

    printf("[+] dlopen ok: %p\n", handle);
    check_symbols(handle, libname, symbols);
    dlclose(handle);
}

int main(void)
{
    static const struct symbol_check dsp_symbols[] = {
        {"IDspService::descriptor", "_ZN6vendor3qti8hardware3dsp4V1_011IDspService10descriptorE"},
        {"IDspService::getService", "_ZN6vendor3qti8hardware3dsp4V1_011IDspService10getServiceERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEb"},
        {"IDspService::tryGetService", "_ZN6vendor3qti8hardware3dsp4V1_011IDspService13tryGetServiceERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEb"},
        {"IDspService::interfaceDescriptor", "_ZN6vendor3qti8hardware3dsp4V1_011IDspService19interfaceDescriptorENSt3__18functionIFvRKN7android8hardware11hidl_stringEEEE"},
        {"IDspService::ping", "_ZN6vendor3qti8hardware3dsp4V1_011IDspService4pingEv"},
        {"BpHwDspService::interfaceDescriptor", "_ZN6vendor3qti8hardware3dsp4V1_014BpHwDspService19interfaceDescriptorENSt3__18functionIFvRKN7android8hardware11hidl_stringEEEE"},
        {"BpHwDspService::getNewDSPSession", "_ZN6vendor3qti8hardware3dsp4V1_014BpHwDspService16getNewDSPSessionEj"},
        {"BpHwDspService::_hidl_getNewDSPSession", "_ZN6vendor3qti8hardware3dsp4V1_014BpHwDspService22_hidl_getNewDSPSessionEPN7android8hardware10IInterfaceEPNS6_7details16HidlInstrumentorEj"},
        {"IDspManager::descriptor", "_ZN6vendor3qti8hardware3dsp4V1_011IDspManager10descriptorE"},
        {"IDspManager::getService", "_ZN6vendor3qti8hardware3dsp4V1_011IDspManager10getServiceERKNSt3__112basic_stringIcNS5_11char_traitsIcEENS5_9allocatorIcEEEEb"},
        {"BpHwDspManager::openSession", "_ZN6vendor3qti8hardware3dsp4V1_014BpHwDspManager11openSessionENS3_9DSPDomainENSt3__18functionIFvRKNS3_10FastrpcDevEEEE"},
        {"BpHwDspManager::_hidl_openSession", "_ZN6vendor3qti8hardware3dsp4V1_014BpHwDspManager17_hidl_openSessionEPN7android8hardware10IInterfaceEPNS6_7details16HidlInstrumentorENS3_9DSPDomainENSt3__18functionIFvRKNS3_10FastrpcDevEEEE"},
        {"FastrpcDev::readEmbeddedFromParcel", "_ZN6vendor3qti8hardware3dsp4V1_022readEmbeddedFromParcelERKNS3_10FastrpcDevERKN7android8hardware6ParcelEmm"},
        {"FastrpcDev::writeEmbeddedToParcel", "_ZN6vendor3qti8hardware3dsp4V1_021writeEmbeddedToParcelERKNS3_10FastrpcDevEPN7android8hardware6ParcelEmm"},
        {NULL, NULL},
    };

    static const struct symbol_check hidl_symbols[] = {
        {"IBase::descriptor", "_ZN7android4hidl4base4V1_05IBase10descriptorE"},
        {"IBase::interfaceDescriptor", "_ZN7android4hidl4base4V1_05IBase19interfaceDescriptorENSt3__18functionIFvRKNS_8hardware11hidl_stringEEEE"},
        {"getRawServiceInternal", "_ZN7android8hardware7details21getRawServiceInternalERKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEESA_bb"},
        {NULL, NULL},
    };

    printf("=== DSP HIDL Symbol Probe ===\n");
    printf("Mode: dlopen/dlsym only; no session or ioctl calls.\n");

    open_and_check("/vendor/lib64/vendor.qti.hardware.dsp@1.0.so", dsp_symbols);
    open_and_check("/system/lib64/libhidlbase.so", hidl_symbols);
    open_and_check("/system/lib64/libhwbinder.so", hidl_symbols);

    return 0;
}