# abi64: build rules, included by ../GNUmakefile (see docs/types-audit.md).
#
# The probes (probe*.c) are compiled against the MINIX headers -- an
# include tree made by mkinc.sh from the source tree, i386 machine/
# headers, with an LP64 shim for 64 -- once per width.  dump32 (the -m32
# probes + dump.c) writes abi64_ref32.h, the i386 reference every variant
# of t_abi64 compares itself with.

ABI64_DIR    := $(CURDIR)/abi64
ABI64_OBJ    := $(HT_OBJ)/abi64
ABI64_PROBES := probe probe_mfs probe_ext2 probe_isofs probe_dev

# -D__i386__: the headers only know i386 and arm; on 64 the probe sees
# the i386 headers with 64-bit long and pointers -- what this test is about.
ABI64_CFLAGS  = -std=gnu99 -O0 -nostdinc -ffreestanding -Wall -Werror \
	-Wno-unused-function -MMD -MP \
	-D__minix=3 -D__minix__=3 -D__unix__=1 -D_MINIX_SYSTEM=1 -D_SYSTEM=1 \
	-D__i386__=1 -D__i386=1 \
	-I$(ABI64_DIR) -I$(ABI64_OBJ)/inc$(1) -I$(TOP)/minix/include \
	-I$(TOP)/include -I$(TOP)/sys -I$(TOP)

# $(1): 32 or 64
define abi64_width
$(ABI64_OBJ)/inc$(1)/.stamp: $(ABI64_DIR)/mkinc.sh $(TOP)/minix/include/minix/ipc.h \
		$(TOP)/sys/arch/i386/include/ansi.h $(TOP)/sys/arch/i386/include/types.h \
		$(TOP)/sys/arch/i386/include/limits.h
	sh $(ABI64_DIR)/mkinc.sh $(TOP) $(ABI64_OBJ)/inc$(1) $(1)
	touch $$@

$(ABI64_OBJ)/$(1)/%.o: $(ABI64_DIR)/%.c $(ABI64_OBJ)/inc$(1)/.stamp
	@mkdir -p $$(@D)
	$(HOSTCC) -m$(1) $$(call ABI64_CFLAGS,$(1)) -c -o $$@ $$<

ABI64_OBJS_$(1) := $(addprefix $(ABI64_OBJ)/$(1)/,$(addsuffix .o,$(ABI64_PROBES)))
-include $(addprefix $(ABI64_OBJ)/$(1)/,$(addsuffix .d,$(ABI64_PROBES)))
endef
$(eval $(call abi64_width,32))
$(eval $(call abi64_width,64))

$(ABI64_OBJ)/dump32: $(ABI64_DIR)/dump.c $(ABI64_DIR)/abi64.h $(ABI64_OBJS_32)
	$(HOSTCC) -m32 -std=gnu99 -Wall -Werror -I$(ABI64_DIR) -o $@ $(filter %.c %.o,$^)

$(ABI64_OBJ)/abi64_ref32.h: $(ABI64_OBJ)/dump32
	$< > $@.tmp && mv $@.tmp $@

# Picked up by the test rule of ../GNUmakefile for t_abi64 in variant $(v).
CFLAGS_abi64 := -I$(ABI64_DIR) -I$(ABI64_OBJ)
$(foreach v,m32 m32-san,$(eval EXTRA_abi64_$(v) := $(ABI64_OBJS_32) $(ABI64_OBJ)/abi64_ref32.h))
$(foreach v,m64 m64-san,$(eval EXTRA_abi64_$(v) := $(ABI64_OBJS_64) $(ABI64_OBJ)/abi64_ref32.h))
