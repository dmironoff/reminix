# Common to the dtc utilities.  Included by each utility's Makefile before
# <bsd.own.mk> (for NOMAN), and again by <bsd.init.mk>: hence the guard.

.if !defined(_DTC_USR_BIN_MAKEFILE_INC_)
_DTC_USR_BIN_MAKEFILE_INC_=1

NOMAN=	# defined

.include "${.CURDIR}/../../Makefile.inc"

CPPFLAGS+=	-I${.CURDIR}/../dtc	# version_gen.h, used by util.c
CPPFLAGS+=	-I${DTCDIST}
CPPFLAGS+=	-I${LIBFDTDIST}
CPPFLAGS+=	-DNO_YAML

.PATH:	${DTCDIST}

.endif
