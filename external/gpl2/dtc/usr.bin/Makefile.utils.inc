# The utilities that use libfdt: link it from its object directory, so that
# the build does not depend on it being installed first.

.include "${DTCDIST}/Makefile.utils"

LIBFDTOBJ!=	cd ${.CURDIR}/../../lib/libfdt && ${PRINTOBJDIR}
DPADD+=		${LIBFDTOBJ}/libfdt.a
LDADD+=		-L${LIBFDTOBJ} -lfdt
