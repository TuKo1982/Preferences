# The modules shipped with UniPrefs, one drawer each with its own Makefile.
# make, make check, make clean here do it in every drawer; one module alone:
# make -C <Name> (or make inside <Name>/).
DIRS = $(patsubst %/Makefile,%,$(wildcard */Makefile))

all check clean:
	@for d in $(DIRS); do $(MAKE) --no-print-directory -C $$d $(filter-out all,$@) || exit 1; done

.PHONY: all check clean
