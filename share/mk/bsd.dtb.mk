#	$NetBSD: bsd.dtb.mk,v 1.7 2026/01/09 18:43:39 skrll Exp $

# Build .dtb files from the device tree sources (docs/devicetree.md, 2.2, 3).
#
# Differences from NetBSD:
# - the sources are devicetree-rebasing, laid out as dist/src/<arch>/<vendor>
#   instead of the Linux dist/arch/<arch>/boot/dts;
# - devicetree-rebasing has no per-vendor Makefiles, so the .dts files are
#   listed explicitly in DTS (no "make -v dtb-y" and no overlay lists).
#
# Variables, set by the sys/dtb/<arch>/Makefile.inc and <vendor>/Makefile:
#	DTSARCH		our own sources: sys/arch/<arch>/dts
#	DTSGNUARCH	upstream source trees, also reachable as dts/<arch>/...
#	DTSSUBDIR	vendor directory; the .dtb files go to ${DTBDIR}/<vendor>
#	DTS		the .dts files to build

.include <bsd.init.mk>

##### Default values
.if !defined(S)
.   if defined(NETBSDSRCDIR)
S=      ${NETBSDSRCDIR}/sys
.   elif defined(BSDSRCDIR)
S=      ${BSDSRCDIR}/sys
.   else
S=      /sys
.   endif
.endif

DTSDIST?=	$S/external/gpl2/dts/dist

##### Basic targets
.PHONY:		dtbinstall dtblist dtb
realinstall:	dtbinstall
realall:	dtb

DTSPADDING?=	1024

.if !make(obj) && !make(clean) && !make(cleandir)
.BEGIN::
	-@mkdir -p ${.OBJDIR}/dts
.for _arch in ${DTSGNUARCH}
	-@ln -snf ${DTSDIST}/src/${_arch} ${.OBJDIR}/dts/${_arch}
.endfor
.endif

DTSINC?=${DTSDIST}/include
.for _arch in ${DTSARCH}
DTSDIR+=$S/arch/${_arch}/dts
.endfor
.for _arch in ${DTSGNUARCH}
DTSDIR+=${DTSDIST}/src/${_arch}
.if defined(DTSSUBDIR)
DTSDIR+=${DTSDIST}/src/${_arch}/${DTSSUBDIR}
.endif
.endfor

DTSPATH=${DTSINC} ${DTSDIR} ${.OBJDIR}/dts

.SUFFIXES: .dtb .dts

# -nostdinc -undef -D__DTS__ as upstream (devicetree-rebasing, Linux kbuild):
# without -undef a cpp that predefines "linux" turns "linux,code" into
# "1,code" (NetBSD's rule has no such flags).
DTSCPPFLAGS?=	-nostdinc -undef -D__DTS__

.dts.dtb:
	${_MKTARGET_CREATE}
	${CPP} -P -xassembler-with-cpp ${DTSCPPFLAGS} ${DTSPATH:@v@-I ${v}@} \
	    -include ${.IMPSRC} /dev/null | \
	${TOOL_DTC} ${DTSPATH:@v@-i ${v}@} -I dts -O dtb \
	    -p ${DTSPADDING} -b 0 -@ -o ${.TARGET}

.PATH.dts: ${DTSDIR}

DTB= 		${DTS:.dts=.dtb}

dtb:		${DTB}

.if defined(DTSSUBDIR)
DTBINSTDIR=	${DTBDIR}/${DTSSUBDIR}
.else
DTBINSTDIR=	${DTBDIR}
.endif

dtbinstall:	dtb
	${INSTALL_DIR} ${DESTDIR}${DTBDIR}
	${INSTALL_DIR} ${DESTDIR}${DTBINSTDIR}
.for _dtb in ${DTB}
	${_MKSHMSG_INSTALL} ${_dtb}
	${_MKSHECHO} "${INSTALL_FILE} -o ${DTBOWN} -g ${DTBGRP} -m ${DTBMODE} \
	    ${.OBJDIR}/${_dtb} ${DESTDIR}${DTBINSTDIR}"
	${INSTALL_FILE} -o ${DTBOWN} -g ${DTBGRP} -m ${DTBMODE} \
	    ${.OBJDIR}/${_dtb} ${DESTDIR}${DTBINSTDIR}
.endfor

# Lines for distrib/sets/lists/minix-base/md.<machine>.
dtblist:
.for _dtb in ${DTB}
	@echo ".${DTBINSTDIR}/${_dtb}"
.endfor

CLEANFILES+=	${DTB}

clean:  .PHONY clean.dts
clean.dts: .PHONY
.for _arch in ${DTSGNUARCH}
	rm -f dts/${_arch}
.endfor
	test -d dts && rmdir dts || true


##### Pull in related .mk logic
.include <bsd.obj.mk>
.include <bsd.kinc.mk>
.include <bsd.clean.mk>
