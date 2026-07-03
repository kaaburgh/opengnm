/* test_drawcmd.c — PM4 command buffer building tests
 *
 * Verifies that the draw command buffer API emits correct PM4 packets.
 * Compares packet headers, opcodes, and key data dwords against
 * expected values derived from shadPS4 and freegnm.
 */
#include "test.h"

#include <string.h>

#include "gnm.h"
#include "gnm_commandbuffer.h"
#include "gnm_drawcommandbuffer.h"
#include "gnm_shader.h"
#include "gnm_types.h"
#include "gnmdriver.h"
#include "pm4/sid.h"

/* PM4 header field extractors */
#define PKT_TYPE(x)     (((x) >> 30) & 0x3)
#define PKT_COUNT(x)    (((x) >> 16) & 0x3FFF)
#define PKT3_OPCODE(x)  (((x) >> 8) & 0xFF)
#define PKT3_PRED(x)    ((x) & 0x1)

static uint32_t s_cmdbuf[4096];

static GnmCommandBuffer new_cmdbuf(void) {
	GnmCommandBuffer cmd = sceGnmCmdInit(
	    s_cmdbuf, sizeof(s_cmdbuf), NULL, NULL
	);
	return cmd;
}

static uint32_t cmd_dwords_used(const GnmCommandBuffer* cmd) {
	return (uint32_t)(cmd->cmdptr - cmd->beginptr);
}

/* --- DrawIndexAuto: emits DRAW_INDEX_AUTO + NOP --- */
static TestResult test_drawindexauto(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdDrawIndexAuto(&cmd, 1024);

	const uint32_t used = cmd_dwords_used(&cmd);
	utasserteq((long long)used, 7LL);

	/* Header: type 3, opcode DRAW_INDEX_AUTO (0x2D), count 1 */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_AUTO);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 1024LL); /* index count */
	/* cmdbuf[2] = SOURCE_SELECT = AUTO_INDEX (0) */
	/* cmdbuf[3..6] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[3]), (long long)PKT3_NOP);
	return test_success();
}

/* --- DrawIndex: emits DRAW_INDEX_2 + NOP --- */
static TestResult test_drawindex(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	const void* fake_addr = (const void*)0x1000;
	sceGnmDrawCmdDrawIndex(&cmd, 256, fake_addr);

	const uint32_t used = cmd_dwords_used(&cmd);
	utasserteq((long long)used, 10LL);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_2);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 4LL);
	utasserteq((long long)s_cmdbuf[1], 256LL); /* index count */
	/* cmdbuf[2] = addr low, cmdbuf[3] = addr high, cmdbuf[4] = max count */
	/* cmdbuf[5] = 0, cmdbuf[6..9] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[6]), (long long)PKT3_NOP);
	return test_success();
}

/* --- DrawIndexAuto with predication enabled --- */
static TestResult test_drawindexauto_predicated(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();
	cmd.flags.predication_enabled = 1;

	GnmDrawModifier mod = {0};
	sceGnmDrawCmdDrawIndexAuto2(&cmd, 512, mod);

	/* Predication bit should be set in the header */
	utasserteq((long long)PKT3_PRED(s_cmdbuf[0]), 1LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_AUTO);
	return test_success();
}

/* --- SetVsShader via driver API --- */
static TestResult test_setvsshader_driver(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmVsStageRegisters regs = {0};
	regs.spishaderpgmlovs = 0x1000;
	regs.spishaderpgmrsrc1vs = 0x40000;
	regs.spishaderpgmrsrc2vs = 0x100;
	regs.paclvsoutcntl = 0x1;
	regs.spivsoutconfig = 0x0;
	regs.spishaderposformat = 0x1;

	int32_t res = sceGnmSetVsShader(s_cmdbuf, 64, (const uint32_t*)&regs, 0);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	/* First packet should be SET_SH_REG for SPI_SHADER_PGM_LO_VS */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_SH_REG);
	return test_success();
}

/* --- SetPsShader via driver API --- */
static TestResult test_setpsshader_driver(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmPsStageRegisters regs = {0};
	regs.spishaderpgmlops = 0x2000;
	regs.spishaderpgmrsrc1ps = 0x40000;

	int32_t res = sceGnmSetPsShader(s_cmdbuf, 64, (const uint32_t*)&regs);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_SH_REG);
	return test_success();
}

/* --- DrawInitDefaultHardwareState350 --- */
static TestResult test_drawinithwstate(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	uint32_t written = sceGnmDrawInitDefaultHardwareState350(
	    s_cmdbuf, sizeof(s_cmdbuf) / sizeof(uint32_t)
	);
	utassert(written > 0);
	/* Should start with a type-3 packet */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	return test_success();
}

/* --- ResetVgtControl: 3 dwords, IA_MULTI_VGT_PARAM --- */
static TestResult test_resetvgtcontrol(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t res = sceGnmResetVgtControl(s_cmdbuf, 3);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_CONTEXT_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	/* Value should be 0xFF (default IA_MULTI_VGT_PARAM) */
	utasserteq((long long)s_cmdbuf[2], 0xFFLL);
	return test_success();
}

/* --- SetVgtControl: validation + value --- */
static TestResult test_setvgtcontrol(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x80, 1, 0);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_CONTEXT_REG);
	/* Expected: (1 << 16) | 0x80 = 0x10080 */
	utasserteq((long long)s_cmdbuf[2], 0x10080LL);
	return test_success();
}

/* --- SetVgtControl: invalid args rejected --- */
static TestResult test_setvgtcontrol_invalid(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	/* prim_group_sz >= 0x100 should fail */
	int32_t res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x100, 0, 0);
	utassert(res != GNM_ERROR_OK);

	/* mode >= 2 should fail */
	res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x10, 2, 0);
	utassert(res != GNM_ERROR_OK);

	/* wrong size should fail */
	res = sceGnmSetVgtControl(s_cmdbuf, 4, 0x10, 0, 0);
	utassert(res != GNM_ERROR_OK);
	return test_success();
}

/* --- DrawIndexOffset: 9 dwords, predication + RT slice --- */
static TestResult test_drawindexoffset(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	/* flags: predication=1, RT slice offset=3 (bits 29-31) */
	uint32_t flags = 1 | (3u << 29);
	int32_t res = sceGnmDrawIndexOffset(s_cmdbuf, 16, 0x100, 256, flags);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_OFFSET_2);
	utasserteq((long long)PKT3_PRED(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 256LL);   /* index count */
	utasserteq((long long)s_cmdbuf[2], 0x100LL); /* index offset */
	utasserteq((long long)s_cmdbuf[4], 3LL);     /* RT slice offset */
	/* cmdbuf[5] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[5]), (long long)PKT3_NOP);
	return test_success();
}

int run_tests_drawcmd(void) {
	const TestUnit tests[] = {
	    {test_drawindexauto, "DrawIndexAuto PM4"},
	    {test_drawindex, "DrawIndex PM4"},
	    {test_drawindexauto_predicated, "DrawIndexAuto predicated"},
	    {test_setvsshader_driver, "SetVsShader driver"},
	    {test_setpsshader_driver, "SetPsShader driver"},
	    {test_drawinithwstate, "DrawInitDefaultHardwareState350"},
	    {test_resetvgtcontrol, "ResetVgtControl"},
	    {test_setvgtcontrol, "SetVgtControl"},
	    {test_setvgtcontrol_invalid, "SetVgtControl invalid args"},
	    {test_drawindexoffset, "DrawIndexOffset"},
	};
	return test_suite(
	    "drawcmd/PM4", tests, sizeof(tests) / sizeof(tests[0])
	);
}
