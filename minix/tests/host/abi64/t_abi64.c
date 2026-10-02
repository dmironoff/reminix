/*
 * abi64: layout of ReMinix types with 32- and 64-bit long and pointers,
 * see docs/types-audit.md.  The probes (probe*.c) are compiled against
 * the MINIX headers for this variant's width; abi64_ref32.h is the same
 * table for -m32 (the layout i386 uses today).
 *
 * On -m32 everything must hold.  On -m64 the known problems are TODO
 * tests, and the counts below are a ratchet: a new message or external
 * structure that breaks on 64 bits fails the run, a fixed one asks for
 * the count or list to be lowered.
 */
#include "hosttest.h"
#include "abi64.h"
#include "abi64_ref32.h"

#define NREF	(sizeof(abi64_ref32) / sizeof(abi64_ref32[0]))
#define LP64	(sizeof(long) == 8)

/* -m64: message types that are not 56 bytes (docs/types-audit.md). */
#define KNOWN_BAD_MSGS_LP64	128

/*
 * -m64: external-layout entries ("type" or "type.field") that differ from
 * i386.  Each is a real 64-bit bug: boot loader, disk or device format.
 */
static const char *const known_bad_ext_lp64[] = {
	/* Multiboot: char * and vaddr_t fields, unpacked uint64_t. */
	"struct multiboot_info",
	"struct multiboot_info.mi_cmdline",
	"struct multiboot_info.mi_mods_count",
	"struct multiboot_info.mi_mods_addr",
	"struct multiboot_info.mi_elfshdr_addr",
	"struct multiboot_info.mi_mmap_length",
	"struct multiboot_info.mi_mmap_addr",
	"struct multiboot_info.mi_drives_addr",
	"struct multiboot_info.mi_loader_name",
	"struct multiboot_info.vbe_control_info",
	"struct multiboot_info.framebuffer_addr",
	"struct multiboot_info.framebuffer_pitch",
	"struct multiboot_info.framebuffer_type",
	"struct multiboot_mmap",
	"struct multiboot_mmap.mm_base_addr",
	"struct multiboot_mmap.mm_length",
	"struct multiboot_mmap.mm_type",
	"struct multiboot_module",
	"struct multiboot_module.mmo_string",
	"struct multiboot_module.mmo_reserved",
	NULL
};

static const struct abi64_ent *
tab_begin(void)
{
	return __start_abi64_tab;
}

static const struct abi64_ent *
tab_end(void)
{
	return __stop_abi64_tab;
}

static int
is_cat(const struct abi64_ent *e, const char *cat)
{
	return strcmp(e->cat, cat) == 0;
}

static const struct abi64_ent *
find_ref(const struct abi64_ent *e)
{
	size_t i;

	for (i = 0; i < NREF; i++)
		if (strcmp(abi64_ref32[i].cat, e->cat) == 0 &&
		    strcmp(abi64_ref32[i].type, e->type) == 0 &&
		    strcmp(abi64_ref32[i].field, e->field) == 0)
			return &abi64_ref32[i];
	return NULL;
}

static int
same_layout(const struct abi64_ent *a, const struct abi64_ent *b)
{
	/* Alignment alone does not move anything: size and offsets do. */
	return a->size == b->size && a->off == b->off;
}

static const char *
ent_name(const struct abi64_ent *e, char *buf, size_t len)
{
	if (e->field[0] != '\0')
		snprintf(buf, len, "%s.%s", e->type, e->field);
	else
		snprintf(buf, len, "%s", e->type);
	return buf;
}

static int
known_bad_ext(const char *name)
{
	const char *const *p;

	if (!LP64)
		return 0;
	for (p = known_bad_ext_lp64; *p != NULL; p++)
		if (strcmp(*p, name) == 0)
			return 1;
	return 0;
}

static void
print_ent(const char *tag, const struct abi64_ent *e,
	const struct abi64_ent *ref)
{
	char name[160];

	printf("# %s %-50s size %lu -> %lu  off %lu -> %lu  align %lu -> %lu\n",
	    tag, ent_name(e, name, sizeof(name)),
	    ref ? ref->size : 0, e->size, ref ? ref->off : 0, e->off,
	    ref ? ref->align : 0, e->align);
}

/* The probe table and the i386 reference describe the same entries. */
TEST(table_matches_reference)
{
	const struct abi64_ent *e;
	int n = 0, msgs = 0;

	for (e = tab_begin(); e < tab_end(); e++) {
		n++;
		msgs += is_cat(e, "msg");
		if (!CHECK(find_ref(e) != NULL))
			print_ent("no reference:", e, NULL);
	}
	CHECK_EQ(n, NREF);
	CHECK(msgs > 200);
	printf("# %d entries, %d message types\n", n, msgs);
}

/* Every message payload type is exactly 56 bytes (ipc.h). */
TEST(messages_56_bytes)
{
	const struct abi64_ent *e;
	int bad = 0;

	if (LP64)
		TODO("message payloads with pointers, vir_bytes, size_t, long");
	for (e = tab_begin(); e < tab_end(); e++) {
		if (!is_cat(e, "msg") || e->size == 56)
			continue;
		print_ent("message", e, find_ref(e));
		bad++;
	}
	printf("# %d message types are not 56 bytes\n", bad);
	CHECK_EQ(bad, 0);
}

/* Ratchet: no new message type breaks on 64 bits. */
TEST(messages_no_new_breakage)
{
	const struct abi64_ent *e;
	int bad = 0;

	for (e = tab_begin(); e < tab_end(); e++)
		bad += is_cat(e, "msg") && e->size != 56;
	if (LP64 && bad < KNOWN_BAD_MSGS_LP64)
		printf("# fixed some: lower KNOWN_BAD_MSGS_LP64 to %d\n", bad);
	CHECK(bad <= (LP64 ? KNOWN_BAD_MSGS_LP64 : 0));
}

/* The message union is 64 bytes (56 payload + source + type). */
TEST(message_union_64_bytes)
{
	const struct abi64_ent *e;
	int seen = 0;

	if (LP64)
		TODO("message union grows with its payloads");
	for (e = tab_begin(); e < tab_end(); e++) {
		if (!is_cat(e, "union"))
			continue;
		seen = 1;
		print_ent("union", e, find_ref(e));
		CHECK_EQ(e->size, 64);
	}
	CHECK(seen);
}

/*
 * External layouts (boot loader, disk, devices) are the same as on
 * i386, except the known 64-bit bugs listed above.
 */
TEST(external_layouts_no_new_breakage)
{
	const struct abi64_ent *e, *ref;
	char name[160];

	for (e = tab_begin(); e < tab_end(); e++) {
		if (!is_cat(e, "ext") || (ref = find_ref(e)) == NULL)
			continue;
		if (same_layout(e, ref)) {
			if (known_bad_ext(ent_name(e, name, sizeof(name))))
				printf("# fixed: remove %s from "
				    "known_bad_ext_lp64\n", name);
			continue;
		}
		if (!known_bad_ext(ent_name(e, name, sizeof(name)))) {
			print_ent("NEW:", e, ref);
			CHECK(0);
		}
	}
}

/* The known external-layout bugs on 64 bits, one TODO for all. */
TEST(external_layouts_known_bugs)
{
	const struct abi64_ent *e, *ref;
	int bad = 0;

	if (LP64)
		TODO("boot loader / device structures with long or pointers");
	for (e = tab_begin(); e < tab_end(); e++) {
		if (!is_cat(e, "ext") || (ref = find_ref(e)) == NULL)
			continue;
		if (!same_layout(e, ref)) {
			print_ent("ext", e, ref);
			bad++;
		}
	}
	CHECK_EQ(bad, 0);
}

/*
 * Kernel/server/process boundary: reported, not checked -- both sides
 * are built for the same width, the open questions are in
 * docs/types-audit.md.
 */
TEST(boundary_report)
{
	const struct abi64_ent *e, *ref;

	for (e = tab_begin(); e < tab_end(); e++) {
		if (!is_cat(e, "bnd") || (ref = find_ref(e)) == NULL)
			continue;
		print_ent(same_layout(e, ref) ? "same   " : "differs", e, ref);
	}
}
