/*
 * PoC: sceGnmCmdInit() leaves GnmCommandBuffer.flags uninitialized, and the
 * draw helpers copy flags.predication_enabled into the PKT3 predicate bit.
 *
 * Build against the generic (host) backend from the repository root:
 *
 *   cp config.generic.mak config.mak && make lib
 *   cc -O0 -std=c11 -Iinclude -Isrc review/poc/uninit_flags.c \
 *      libopengnm.a -lm -o uninit_flags && ./uninit_flags
 *
 * Observed with gcc 13 -O0 on x86_64 Linux:
 *   flags.predication_enabled=1  PKT3 header=0xc0012d01 predicate_bit=1
 * Expected: predicate_bit=0 (predication was never requested).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gnm_commandbuffer.h"
#include "gnm_drawcommandbuffer.h"
#include "platform.h"

static uint32_t buf[256];

__attribute__((noinline)) static void dirty_stack(void) {
	volatile unsigned char junk[4096];
	memset((void*)junk, 0xff, sizeof(junk));
}

__attribute__((noinline)) static int run(void) {
	GnmCommandBuffer cmd = sceGnmCmdInit(buf, sizeof(buf), NULL, NULL);
	sceGnmDrawCmdDrawIndexAuto(&cmd, 3);
	printf(
	    "flags.predication_enabled=%u  PKT3 header=0x%08x predicate_bit=%u\n",
	    (unsigned)cmd.flags.predication_enabled, buf[0], buf[0] & 1u
	);
	return (int)(buf[0] & 1u);
}

int main(void) {
	sceGnmPlatInit(NULL);
	dirty_stack();
	return run();
}
