# fdt: build rules, included by ../GNUmakefile (see docs/devicetree.md, 3).
#
# The tests parse the .dtb of the reference board (Orange Pi PC Plus) with
# the part of libfdt the earm kernel links (minix/kernel/arch/earm/
# Makefile.inc): fdt.c, fdt_ro.c, fdt_strerror.c -- if a test needs more, it
# does not link.  The .dtb is made here, independent of an earm build: a host
# dtc from the imported sources, then cpp | dtc with the flags of
# share/mk/bsd.dtb.mk.

FDT_OBJ   := $(HT_OBJ)/fdt
LIBFDT    := sys/external/bsd/libfdt/dist
DTCDIST   := $(TOP)/external/gpl2/dtc/dist
DTCGEN    := $(TOP)/external/gpl2/dtc/usr.bin/dtc
DTSDIST   := $(TOP)/sys/external/gpl2/dts/dist
FDT_BOARD := allwinner/sun8i-h3-orangepi-pc-plus
FDT_DTB   := $(FDT_OBJ)/$(notdir $(FDT_BOARD)).dtb

SRCS_fdt   := $(LIBFDT)/fdt.c $(LIBFDT)/fdt_ro.c $(LIBFDT)/fdt_strerror.c
CFLAGS_fdt := -I$(TOP)/$(LIBFDT) -DFDT_DTB='"$(FDT_DTB)"'

FDT_DTC_SRCS := $(addprefix $(DTCDIST)/,checks.c data.c dtc.c flattree.c \
	fstree.c livetree.c srcpos.c treesource.c util.c) \
	$(DTCGEN)/dtc-lexer.lex.c $(DTCGEN)/dtc-parser.tab.c

$(FDT_OBJ)/dtc: $(FDT_DTC_SRCS)
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -w -DNO_YAML -I$(DTCGEN) -I$(DTCDIST) -I$(TOP)/$(LIBFDT) \
		-o $@ $(FDT_DTC_SRCS)

# As .dts.dtb in bsd.dtb.mk, with sys/dtb/arm/Makefile.inc (DTSGNUARCH).
FDT_DTSPATH := $(DTSDIST)/include $(foreach a,arm arm64 riscv,$(DTSDIST)/src/$(a) \
	$(DTSDIST)/src/$(a)/$(dir $(FDT_BOARD)))

$(FDT_DTB): $(FDT_OBJ)/dtc $(DTSDIST)/src/arm/$(FDT_BOARD).dts
	cpp -P -xassembler-with-cpp -nostdinc -undef -D__DTS__ \
		$(addprefix -I ,$(FDT_DTSPATH)) \
		-include $(DTSDIST)/src/arm/$(FDT_BOARD).dts /dev/null | \
	$(FDT_OBJ)/dtc $(addprefix -i ,$(FDT_DTSPATH)) -I dts -O dtb \
		-p 1024 -b 0 -@ -o $@

$(foreach v,$(VARIANTS),$(eval EXTRA_fdt_$(v) := $(FDT_DTB)))
