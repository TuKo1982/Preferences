# About.module alone: make here, or make -C modules/About from UniPrefs' top
UNIPREFS_SDK ?= ../../..
PREFIX ?= $(HOME)/ApolloCrossDev/Compilers/GCC-6.50-Dev
AP_CC ?= $(firstword $(wildcard $(PREFIX)/bin/m68k-amigaos-gcc) m68k-amigaos-gcc)
MODULES = About
include $(UNIPREFS_SDK)/module.mk
