#	$NetBSD: Makefile,v 1.10 2026/03/31 20:24:19 yurix Exp $

# .dtb files for the platforms booted with a flattened device tree
# (docs/devicetree.md, 3).  After changing the lists, update
# distrib/sets/lists/minix-base/md.<machine>: "make dtblist" prints the lines.

TARGETS+=	dtblist

.include <bsd.own.mk>

.if !empty(MACHINE_ARCH:Mearm*)
SUBDIR+=	arm
.endif

.include <bsd.subdir.mk>
