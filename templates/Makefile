# <Name>.module: copy this Makefile next to <Name>.c (one drawer per module),
# set MODULES, then: make, make check, make clean.
UNIPREFS_SDK ?= /path/to/UniPrefs-SDK
# the cross compiler: m68k-amigaos-gcc in the PATH, or AP_CC=... on the command line
MODULES = Template
# optional, before the include line:
# AP_INCS = ../common                   extra include directories (module and make check)
# AP_LIBS_Template = -lm                extra link libraries
# AP_EXTRA_Template = $(AP_OBJ)/more.o  extra objects (more.c here is built like Template.c)
include $(UNIPREFS_SDK)/module.mk
