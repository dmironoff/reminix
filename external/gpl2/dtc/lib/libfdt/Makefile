#	$NetBSD: Makefile,v 1.3 2017/06/08 18:57:21 skrll Exp $

# Unlike NetBSD (LIBISPRIVATE), ReMinix installs libfdt and its headers:
# docs/devicetree.md, 3.1.

.include <bsd.own.mk>

LIB=	fdt

.include "${.CURDIR}/../../Makefile.inc"
.include "${LIBFDTDIST}/Makefile.libfdt"

SRCS=	${LIBFDT_SRCS}

CPPFLAGS+=	-I ${LIBFDTDIST}

INCS=		${LIBFDT_INCLUDES}
INCSDIR=	/usr/include

.PATH:	${LIBFDTDIST}

.include <bsd.lib.mk>
