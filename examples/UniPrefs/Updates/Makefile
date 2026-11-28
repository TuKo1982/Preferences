# Updates.module alone: make here, or make -C modules/Updates from UniPrefs' top
UNIPREFS_SDK ?= ../../..
PREFIX ?= $(HOME)/ApolloCrossDev/Compilers/GCC-6.50-Dev
AP_CC ?= $(firstword $(wildcard $(PREFIX)/bin/m68k-amigaos-gcc) m68k-amigaos-gcc)
MODULES = Updates
# Updates.c includes "Updates/updcore.h" and "Updates/updnet.h"
AP_INCS = ..
# its network code (DropBox-Handler 2.0's httpconn, adapted), built with the AmiSSL 5 SDK
AMISSL_SDK ?= $(HOME)/ApolloCrossDev/SDKs/AmiSSL-5.27/AmiSSL/Developer
AP_EXTRA_Updates = $(AP_OBJ)/updnet.o
include $(UNIPREFS_SDK)/module.mk

$(AP_OBJ)/updnet.o: updnet.c updnet.h | $(AP_OBJ)
	$(AP_CC) -O2 $(AP_CPU) -noixemul -fno-common -Wall -Wno-pointer-sign -fomit-frame-pointer -DOPENSSL_SYS_AMIGA \
	    -I$(AMISSL_SDK)/include -c -o $@ $<
$(AP_OBJ)/Updates.o: updnet.h updcore.h
