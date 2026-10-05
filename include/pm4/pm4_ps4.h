#ifndef _GNM_PM4_PS4_H_
#define _GNM_PM4_PS4_H_

typedef enum {
	GPU_OPHINT_SET_VSHARP_USERDATA = 0x68750004,
	GPU_OPHINT_SET_TSHARP_USERDATA = 0x68750005,
	GPU_OPHINT_SET_SSHARP_USERDATA = 0x68750006,
} GpuOpHint;

/* EVENT_WRITE_EOP INT_SEL: raise the interrupt once the data write is
 * confirmed. Mesa's EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM (3) does not occur
 * in PS4 command streams, and shadPS4 aborts on it. */
#define EOP_INT_SEL_SEND_INT_ON_CONFIRM 2

#endif	// _GNM_PM4_PS4_H_
