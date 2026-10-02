# Host unit tests: ReMinix code built and tested on Linux with the host
# gcc, see docs/testing.md 1д.  GNU make (bmake of the NetBSD tree does
# not read GNUmakefile, build.sh never comes here).
#
#   make check                       all modules, all variants
#   make check MODULES="bitmap"      only these modules (subdirectories)
#   make check VARIANTS="m64-san"    only these build variants
#
# A module is a subdirectory with t_*.c test programs (each includes
# hosttest.h).  Code under test is compiled from its place in the tree:
# list it as SRCS_<module> (paths from the top of the tree), extra flags
# as CFLAGS_<module>.  Needs gcc with multilib (-m32) and libasan/libubsan.

TOP      := $(abspath $(CURDIR)/../../..)
HT_OBJ   ?= $(TOP)/obj/host-tests
HOSTCC   ?= gcc

MODULES  ?= $(patsubst %/,%,$(sort $(dir $(wildcard */t_*.c))))
VARIANTS ?= m32 m64 m32-san m64-san

# -MMD -MP: header dependencies (the headers under test included).
CFLAGS_BASE  := -std=gnu99 -g -Wall -Wextra -Werror -MMD -MP \
	-I$(CURDIR) -I$(TOP)/minix/include
SANITIZE     := -O1 -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
CFLAGS_m32     := -m32 -O2
CFLAGS_m64     := -m64 -O2
CFLAGS_m32-san := -m32 $(SANITIZE)
CFLAGS_m64-san := -m64 $(SANITIZE)

# Code under test per module (none yet: bitmap is header-only).
SRCS_bitmap  :=

TESTS := $(foreach m,$(MODULES),$(basename $(wildcard $(m)/t_*.c)))
BINS  := $(foreach v,$(VARIANTS),$(addprefix $(HT_OBJ)/$(v)/,$(TESTS)))

# $(1) variant, $(2) module/t_name
define test_rule
$(HT_OBJ)/$(1)/$(2): $(2).c $(addprefix $(TOP)/,$(SRCS_$(patsubst %/,%,$(dir $(2))))) hosttest.h
	@mkdir -p $$(@D)
	$(HOSTCC) $(CFLAGS_BASE) $(CFLAGS_$(1)) \
		$(CFLAGS_$(patsubst %/,%,$(dir $(2)))) -o $$@ $$(filter %.c,$$^)
endef
$(foreach v,$(VARIANTS),$(foreach t,$(TESTS),$(eval $(call test_rule,$(v),$(t)))))

-include $(addsuffix .d,$(BINS))

.PHONY: all check clean
all: $(BINS)

check: all
	@sh $(CURDIR)/run.sh $(HT_OBJ) $(BINS)

clean:
	rm -rf $(HT_OBJ)
