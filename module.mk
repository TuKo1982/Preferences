# UniPrefs module SDK - make rules for binary modules.
#
# Your Makefile, next to Hello.c:
#     UNIPREFS_SDK = /path/to/UniPrefs-SDK
#     MODULES      = Hello                 # Hello.c -> Hello.module
#     include $(UNIPREFS_SDK)/module.mk
# then:  make            Hello.module, for Modules/
#        make check      checks the descriptor on the build machine (cc)
#        make clean
#
# Settings (all optional):
#     AP_CC     the Amiga cross compiler      (m68k-amigaos-gcc, GCC 6.5 / libnix)
#     AP_CPU    -m68020 ...                    (-m68020: runs on every UniPrefs machine)
#     AP_SRC    where the .c files are         (.)
#     AP_OUT    where the .module files go     (.)
#     AP_OBJ    object files                   (obj)
#     HOSTCC    compiler of the build machine  (cc)
#     AP_LIBS_<Name>  extra link libraries of one module, e.g. AP_LIBS_AHI = -lm
#               (libnix soft float: the module opens mathieeedoubbas.library itself)
#     AP_INCS   extra include directories, for the module and for make check
#               (e.g. AP_INCS = ../common for headers shared by several modules)
#     AP_EXTRA_<Name>  extra object files linked into one module, after glue.o,
#               e.g. AP_EXTRA_Hello = $(AP_OBJ)/net.o (net.c next to Hello.c is
#               built like Hello.c; other flags: your own rule after the include)
# Set them before the include line. One module per drawer, each with its own
# Makefile, is how UniPrefs builds its modules (examples/UniPrefs/<Name>/).

UNIPREFS_SDK ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
AP_CC   ?= m68k-amigaos-gcc
AP_CPU  ?= -m68020
AP_SRC  ?= .
AP_OUT  ?= .
AP_OBJ  ?= obj
HOSTCC  ?= cc
AP_INC  := $(UNIPREFS_SDK)/include
AP_HOSTSRC ?= $(UNIPREFS_SDK)/tools

AP_CFLAGS ?= -O2 -Wall -Wno-pointer-sign -Wno-missing-field-initializers -fomit-frame-pointer
AP_INCFLAGS := $(addprefix -I,$(AP_INCS))
AP_MODCFLAGS := $(AP_CPU) -noixemul -fno-common -DAP_PLUGIN -I$(AP_INC) $(AP_INCFLAGS) $(AP_CFLAGS)
AP_MODULES := $(addprefix $(AP_OUT)/,$(addsuffix .module,$(MODULES)))

modules: $(AP_MODULES)

$(AP_OBJ) $(AP_OUT) $(AP_OBJ)/check:
	mkdir -p $@

# modstart.o comes first: its header must open the first hunk (apmodule.h);
# no start-up code, the C runtime is UniPrefs' (glue.c)
$(AP_OBJ)/modstart.o: $(UNIPREFS_SDK)/lib/modstart.S $(AP_INC)/apabi.h | $(AP_OBJ)
	$(AP_CC) $(AP_CPU) -I$(AP_INC) -c -o $@ $<

$(AP_OBJ)/glue.o: $(UNIPREFS_SDK)/lib/glue.c $(AP_INC)/*.h | $(AP_OBJ)
	$(AP_CC) $(AP_MODCFLAGS) -c -o $@ $<

# the headers of AP_INCS too (modules/common/*.h shared by several modules): a change there rebuilds them
AP_INCDEPS := $(foreach d,$(AP_INCS),$(wildcard $(d)/*.h))
$(AP_OBJ)/%.o: $(AP_SRC)/%.c $(AP_INC)/*.h $(AP_INCDEPS) | $(AP_OBJ)
	$(AP_CC) $(AP_MODCFLAGS) -c -o $@ $<

$(AP_OUT)/%.module: $(AP_OBJ)/modstart.o $(AP_OBJ)/%.o $(AP_OBJ)/glue.o | $(AP_OUT)
	$(AP_CC) $(AP_CPU) -noixemul -s -nostartfiles -o $@ $^ $(AP_LIBS_$*) -lamiga

# extra objects of a module (AP_EXTRA_<Name>), linked after glue.o
$(foreach m,$(MODULES),$(if $(AP_EXTRA_$(m)),$(eval $(AP_OUT)/$(m).module: $(AP_EXTRA_$(m)))))

# the same checks UniPrefs makes when it loads the module, on the build
# machine: the keys part of each module, built without its GUI (AP_NO_GUI)
# with UniPrefs' own key engine. obj/check/<Name> [file] also shows every key
# as read from a preferences file (default: a new file).
AP_CORE := $(addprefix $(AP_HOSTSRC)/,reg_core.c prefsfile.c keys.c state.c) \
           $(UNIPREFS_SDK)/tools/apcheck.c $(UNIPREFS_SDK)/tools/host_io.c

check: $(addprefix $(AP_OBJ)/check/,$(MODULES))
	@for m in $^; do $$m || exit 1; done

$(AP_OBJ)/check/%: $(AP_SRC)/%.c $(AP_CORE) $(AP_INC)/*.h | $(AP_OBJ)/check
	$(HOSTCC) -Wall -DAP_NO_GUI -DAP_CHECK -I$(AP_INC) -I$(AP_HOSTSRC) $(AP_INCFLAGS) -o $@ $< $(AP_CORE)

clean::
	rm -rf $(AP_OBJ) $(AP_MODULES)

.PHONY: modules check clean
.SECONDARY:
