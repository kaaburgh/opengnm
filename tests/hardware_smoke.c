#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <orbis/libkernel.h>

#include "gnm_commandbuffer.h"
#include "gnm_drawcommandbuffer.h"
#include "gnmdriver.h"

#ifndef ORBIS_KERNEL_WC_GARLIC
#define ORBIS_KERNEL_WC_GARLIC 3
#endif

#ifndef ORBIS_KERNEL_PROT_CPU_READ
#define ORBIS_KERNEL_PROT_CPU_READ 0x01
#endif

#ifndef ORBIS_KERNEL_PROT_CPU_RW
#define ORBIS_KERNEL_PROT_CPU_RW 0x02
#endif

#ifndef ORBIS_KERNEL_PROT_GPU_READ
#define ORBIS_KERNEL_PROT_GPU_READ 0x10
#endif

#ifndef ORBIS_KERNEL_PROT_GPU_WRITE
#define ORBIS_KERNEL_PROT_GPU_WRITE 0x20
#endif

enum {
    kDirectMemorySize = 64 * 1024,
    kDirectMemoryAlignment = 2 * 1024 * 1024,
    kCommandDwords = 4096,
};

static int allocate_garlic(void** out_addr, int64_t* out_direct_memory) {
    int res = sceKernelAllocateDirectMemory(0, UINT64_MAX, kDirectMemorySize,
                                            kDirectMemoryAlignment,
                                            ORBIS_KERNEL_WC_GARLIC, out_direct_memory);
    if (res < 0) {
        printf("sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return res;
    }

    const int protection = ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
                           ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE;
    res = sceKernelMapDirectMemory(out_addr, kDirectMemorySize, protection, 0,
                                   *out_direct_memory, kDirectMemoryAlignment);
    if (res < 0) {
        printf("sceKernelMapDirectMemory failed: 0x%x\n", res);
        return res;
    }

    return 0;
}

int main(void) {
    void* mapped = NULL;
    int64_t direct_memory = 0;
    int res = allocate_garlic(&mapped, &direct_memory);
    if (res < 0) {
        return 1;
    }

    volatile uint64_t* eop_label =
        (volatile uint64_t*)((uint8_t*)mapped + kDirectMemorySize - sizeof(uint64_t));
    *eop_label = 0;

    GnmCommandBuffer cmd =
        sceGnmCmdInit(mapped, kCommandDwords, NULL, NULL);
    sceGnmDrawCmdInitDefaultHardwareState(&cmd);
    sceGnmDrawCmdDrawIndexAuto(&cmd, 0);
    sceGnmDrawCmdEventWriteEop(&cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
                               (uint64_t)(uintptr_t)eop_label,
                               GNM_DATA_SEL_SEND_DATA64, 0x4f50474e534d4b45ULL);

    const uint32_t dcb_size = (uint32_t)((uintptr_t)cmd.cmdptr - (uintptr_t)cmd.beginptr);
    const uint32_t* dcb_addrs[1] = {cmd.beginptr};
    uint32_t dcb_sizes[1] = {dcb_size};

    res = sceGnmSubmitCommandBuffers(1, dcb_addrs, dcb_sizes, NULL, NULL);
    if (res < 0) {
        printf("sceGnmSubmitCommandBuffers failed: 0x%x\n", res);
        return 2;
    }

    res = sceGnmSubmitDone();
    if (res < 0) {
        printf("sceGnmSubmitDone failed: 0x%x\n", res);
        return 3;
    }

    for (uint32_t spin = 0; spin < 100000000; ++spin) {
        if (*eop_label == 0x4f50474e534d4b45ULL) {
            printf("opengnm hardware smoke passed: dcb_size=%u\n", dcb_size);
            return 0;
        }
    }

    printf("EOP label was not written; last value=0x%llx\n",
           (unsigned long long)*eop_label);
    return 4;
}
