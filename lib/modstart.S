| modstart - must be the first object of every binary module: the start of
| the first code hunk is what UniPrefs checks (apmodule.h). Preprocessed:
| the numbers come from apabi.h.
#include "apabi.h"
	.text
	.globl	_ap_start
_ap_start:
	moveq	#-1,d0
	rts
	.long	AP_MAGIC
	.long	AP_ABI
	.long	_ap_init
