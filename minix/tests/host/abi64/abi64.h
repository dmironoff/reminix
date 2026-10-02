/*
 * abi64: one table entry of the probe (probe*.c).  Plain C types only: the
 * probes are compiled against the MINIX headers, the readers (t_abi64.c,
 * dump.c) against glibc.
 *
 * Categories:
 *   msg   every IPC payload type, through _ASSERT_MSG_SIZE in ipc.h
 *   union the message union itself (sizeof(message) check in ipc.h)
 *   ext   layout fixed from outside -- boot loader, disk, device: must
 *         not depend on the width of long and pointers
 *   bnd   kernel/server/process boundary: reported, not checked (see
 *         docs/types-audit.md)
 */
#ifndef ABI64_H
#define ABI64_H

struct abi64_ent {
	const char *cat;	/* msg, union, ext, bnd */
	const char *type;	/* type name as written */
	const char *field;	/* "" for the type itself */
	unsigned long size;	/* sizeof the type or field */
	unsigned long align;	/* alignment of the type (0 for a field) */
	unsigned long off;	/* offset of the field */
};

/* Provided by the linker for the "abi64_tab" section. */
extern const struct abi64_ent __start_abi64_tab[], __stop_abi64_tab[];

#ifdef ABI64_PROBE
#define ABI64_JOIN2(a, b)	a##b
#define ABI64_JOIN(a, b)	ABI64_JOIN2(a, b)
#define ABI64_ENT(cat, tname, fname, sz, al, of)			\
	static const struct abi64_ent					\
	__attribute__((used, section("abi64_tab"),			\
	    aligned(__alignof__(struct abi64_ent))))			\
	ABI64_JOIN(abi64_e_, __COUNTER__) = {				\
		cat, tname, fname, sz, al, of				\
	}
#define ABI64_TYPE(cat, T)						\
	ABI64_ENT(cat, #T, "", sizeof(T), __alignof__(T), 0)
#define ABI64_FIELD(cat, T, f)						\
	ABI64_ENT(cat, #T, #f, sizeof(((T *)0)->f), 0,			\
	    __builtin_offsetof(T, f))
/* Offset only (flexible array members). */
#define ABI64_OFFSET(cat, T, f)						\
	ABI64_ENT(cat, #T, #f, 0, 0, __builtin_offsetof(T, f))
/* End of the part of T that lives on disk: offset + size of field f. */
#define ABI64_DISKEND(T, f)						\
	ABI64_ENT("ext", #T, "<on-disk end>", 0, 0,			\
	    __builtin_offsetof(T, f) + sizeof(((T *)0)->f))

/*
 * The compile-time message size checks of ipc.h fail on 64 bits; here
 * they record the sizes instead (only in the probe that defines
 * ABI64_MSGS, so that each message is in the table once) or vanish.
 */
#include <minix/ipcconst.h>
#undef _ASSERT_MSG_SIZE
#ifdef ABI64_MSGS
#define _ASSERT_MSG_SIZE(T)	ABI64_TYPE("msg", T);
#define _ASSERT_MSG_UNION(T)	ABI64_TYPE("union", T)
#else
#define _ASSERT_MSG_SIZE(T)	struct abi64_unused
#define _ASSERT_MSG_UNION(T)	struct abi64_unused
#endif
#endif /* ABI64_PROBE */

#endif /* ABI64_H */
