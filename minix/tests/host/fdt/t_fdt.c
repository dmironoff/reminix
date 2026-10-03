/*
 * libfdt as the earm kernel links it (fdt.c, fdt_ro.c, fdt_strerror.c; see
 * minix/kernel/arch/earm/Makefile.inc and docs/devicetree.md, 3), on the
 * .dtb of the reference board, Orange Pi PC Plus (Allwinner H3), built by
 * module.mk the way share/mk/bsd.dtb.mk builds it.
 *
 * The tests do what the early boot code will need: check the blob, identify
 * the board, find the CPUs, the console (chosen/stdout-path through an
 * alias), decode "reg" with #address-cells/#size-cells, follow phandles.
 * They also reject damaged blobs.
 */
#include "hosttest.h"

#include <libfdt.h>

static void *fdt;
static size_t fdt_len;

/* Read FDT_DTB into a buffer with some room after the blob. */
static void
load(void)
{
	FILE *f;
	long n;

	REQUIRE((f = fopen(FDT_DTB, "rb")) != NULL);
	REQUIRE(fseek(f, 0, SEEK_END) == 0);
	REQUIRE((n = ftell(f)) > 0);
	rewind(f);
	fdt_len = (size_t)n;
	REQUIRE((fdt = malloc(fdt_len + 4096)) != NULL);
	REQUIRE(fread(fdt, 1, fdt_len, f) == fdt_len);
	fclose(f);
}

/* A copy of the blob to damage. */
static void *
copy(void)
{
	void *p = malloc(fdt_len);

	if (p != NULL)
		memcpy(p, fdt, fdt_len);
	return p;
}

/*
 * #address-cells or #size-cells of a node (the cells of its children's
 * "reg"), with the defaults of the specification (2 and 1).  fdt_address_cells()
 * is in fdt_addresses.c, which the kernel does not link (it needs fdt_rw.c).
 */
static int
cells(int node, const char *name, int dflt)
{
	const fdt32_t *p;
	int len;

	if ((p = fdt_getprop(fdt, node, name, &len)) == NULL)
		return dflt;
	if (len != sizeof(*p))
		return -1;
	return (int)fdt32_to_cpu(*p);
}

/* Value of n cells (n = 1 or 2) at p. */
static uint64_t
read_cells(const fdt32_t *p, int n)
{
	uint64_t v = 0;

	while (n-- > 0)
		v = (v << 32) | fdt32_to_cpu(*p++);
	return v;
}

/* The first (address, size) pair of the "reg" of a node. */
static int
reg0(int node, uint64_t *addr, uint64_t *size)
{
	const fdt32_t *p;
	int parent, ac, sc, len;

	if ((parent = fdt_parent_offset(fdt, node)) < 0)
		return parent;
	ac = cells(parent, "#address-cells", 2);
	sc = cells(parent, "#size-cells", 1);
	if (ac < 1 || ac > 2 || sc < 0 || sc > 2)
		return -FDT_ERR_BADNCELLS;
	if ((p = fdt_getprop(fdt, node, "reg", &len)) == NULL)
		return len;
	if (len < (int)((ac + sc) * sizeof(*p)))
		return -FDT_ERR_BADVALUE;
	*addr = read_cells(p, ac);
	*size = sc ? read_cells(p + ac, sc) : 0;
	return 0;
}

TEST(header)
{
	load();
	CHECK_EQ(fdt_check_header(fdt), 0);
	CHECK_EQ(fdt_magic(fdt), FDT_MAGIC);
	CHECK_EQ(fdt_version(fdt), 17);
	CHECK_EQ(fdt_last_comp_version(fdt), 16);
	CHECK_EQ(fdt_totalsize(fdt), fdt_len);
	CHECK_EQ(fdt_boot_cpuid_phys(fdt), 0);
	CHECK_EQ(fdt_num_mem_rsv(fdt), 0);
	/* -p 1024: room for U-Boot to add /memory, /chosen properties. */
	CHECK(fdt_off_dt_strings(fdt) + fdt_size_dt_strings(fdt) + 1024 <=
	    fdt_totalsize(fdt));
}

TEST(board)
{
	const char *model;
	int len;

	load();
	CHECK_EQ(fdt_node_check_compatible(fdt, 0, "xunlong,orangepi-pc-plus"), 0);
	CHECK_EQ(fdt_node_check_compatible(fdt, 0, "allwinner,sun8i-h3"), 0);
	CHECK_EQ(fdt_node_check_compatible(fdt, 0, "xunlong,orangepi-pc"), 1);
	CHECK_EQ(fdt_stringlist_count(fdt, 0, "compatible"), 2);
	CHECK(strcmp(fdt_stringlist_get(fdt, 0, "compatible", 0, &len),
	    "xunlong,orangepi-pc-plus") == 0);
	REQUIRE((model = fdt_getprop(fdt, 0, "model", &len)) != NULL);
	CHECK(strcmp(model, "Xunlong Orange Pi PC Plus") == 0);
	CHECK_EQ(len, strlen(model) + 1);
	CHECK_EQ(cells(0, "#address-cells", 2), 1);
	CHECK_EQ(cells(0, "#size-cells", 1), 1);
}

TEST(cpus)
{
	const char *type;
	uint64_t a, s;
	int cpus, node, n = 0, len;

	load();
	REQUIRE((cpus = fdt_path_offset(fdt, "/cpus")) >= 0);
	CHECK_EQ(cells(cpus, "#address-cells", 2), 1);
	CHECK_EQ(cells(cpus, "#size-cells", 1), 0);
	fdt_for_each_subnode(node, fdt, cpus) {
		type = fdt_getprop(fdt, node, "device_type", &len);
		if (type == NULL || strcmp(type, "cpu") != 0)
			continue;
		CHECK_EQ(fdt_node_check_compatible(fdt, node, "arm,cortex-a7"), 0);
		CHECK_EQ(reg0(node, &a, &s), 0);
		CHECK_EQ(a, n);		/* MPIDR affinity: 0..3 */
		n++;
	}
	CHECK_EQ(node, -FDT_ERR_NOTFOUND);
	CHECK_EQ(n, 4);
}

/* No /memory upstream: U-Boot adds it (docs/devicetree.md, 3.2). */
TEST(no_memory_node)
{
	load();
	CHECK_EQ(fdt_path_offset(fdt, "/memory"), -FDT_ERR_NOTFOUND);
	CHECK_EQ(fdt_node_offset_by_prop_value(fdt, -1, "device_type",
	    "memory", sizeof("memory")), -FDT_ERR_NOTFOUND);
}

/* The console: /chosen/stdout-path -> alias serial0 -> /soc/serial@1c28000. */
TEST(console)
{
	const char *path, *alias;
	char name[64];
	uint64_t a, s;
	int chosen, node, len;
	size_t n;

	load();
	REQUIRE((chosen = fdt_path_offset(fdt, "/chosen")) >= 0);
	REQUIRE((path = fdt_getprop(fdt, chosen, "stdout-path", &len)) != NULL);
	CHECK(strcmp(path, "serial0:115200n8") == 0);

	/* The part before ':' is a path or an alias. */
	n = strcspn(path, ":");
	REQUIRE(n < sizeof(name));
	memcpy(name, path, n);
	name[n] = '\0';
	REQUIRE((alias = fdt_get_alias(fdt, name)) != NULL);
	CHECK(strcmp(alias, "/soc/serial@1c28000") == 0);
	CHECK_EQ(fdt_path_offset_namelen(fdt, path, (int)n),
	    fdt_path_offset(fdt, alias));

	REQUIRE((node = fdt_path_offset(fdt, alias)) >= 0);
	CHECK_EQ(fdt_node_check_compatible(fdt, node, "snps,dw-apb-uart"), 0);
	CHECK_EQ(reg0(node, &a, &s), 0);
	CHECK_EQ(a, 0x01c28000);
	CHECK_EQ(s, 0x400);
	CHECK_EQ(fdt32_to_cpu(*(const fdt32_t *)fdt_getprop(fdt, node,
	    "reg-shift", NULL)), 2);
}

/* interrupt-parent of the root is a phandle of the GIC. */
TEST(phandles)
{
	const fdt32_t *p;
	uint32_t ph;
	uint64_t a, s;
	int gic, len;

	load();
	REQUIRE((p = fdt_getprop(fdt, 0, "interrupt-parent", &len)) != NULL);
	CHECK_EQ(len, sizeof(*p));
	ph = fdt32_to_cpu(*p);
	REQUIRE((gic = fdt_node_offset_by_phandle(fdt, ph)) >= 0);
	CHECK_EQ(fdt_get_phandle(fdt, gic), ph);
	CHECK_EQ(fdt_node_check_compatible(fdt, gic, "arm,gic-400"), 0);
	CHECK(fdt_getprop(fdt, gic, "interrupt-controller", NULL) != NULL);
	CHECK_EQ(reg0(gic, &a, &s), 0);
	CHECK_EQ(a, 0x01c81000);	/* distributor */
	CHECK_EQ(gic, fdt_node_offset_by_compatible(fdt, -1, "arm,gic-400"));
	CHECK(fdt_node_offset_by_compatible(fdt, -1, "arm,armv7-timer") >= 0);
}

/* -@: labels are kept for overlays. */
TEST(symbols)
{
	int sym, len;
	const char *p;

	load();
	REQUIRE((sym = fdt_path_offset(fdt, "/__symbols__")) >= 0);
	REQUIRE((p = fdt_getprop(fdt, sym, "uart0", &len)) != NULL);
	CHECK(strcmp(p, "/soc/serial@1c28000") == 0);
	REQUIRE((p = fdt_getprop(fdt, sym, "cpu0", &len)) != NULL);
	CHECK(strcmp(p, "/cpus/cpu@0") == 0);
}

/* Damaged blobs are rejected, not followed. */
TEST(damaged)
{
	unsigned char *bad;

	load();
	REQUIRE((bad = copy()) != NULL);
	bad[0] ^= 0xff;			/* magic */
	CHECK_EQ(fdt_check_header(bad), -FDT_ERR_BADMAGIC);
	CHECK_EQ(fdt_path_offset(bad, "/cpus"), -FDT_ERR_BADMAGIC);
	free(bad);

	REQUIRE((bad = copy()) != NULL);
	((fdt32_t *)bad)[5] = cpu_to_fdt32(1);	/* version 1 */
	CHECK_EQ(fdt_check_header(bad), -FDT_ERR_BADVERSION);
	free(bad);

	REQUIRE((bad = copy()) != NULL);
	((fdt32_t *)bad)[1] = cpu_to_fdt32(16);	/* totalsize < header */
	CHECK(fdt_check_header(bad) < 0);
	free(bad);

	REQUIRE((bad = copy()) != NULL);
	((fdt32_t *)bad)[3] = cpu_to_fdt32(fdt_len + 4096);	/* strings past end */
	CHECK_EQ(fdt_check_header(bad), -FDT_ERR_TRUNCATED);
	free(bad);

	CHECK(strcmp(fdt_strerror(-FDT_ERR_BADMAGIC), "FDT_ERR_BADMAGIC") == 0);
	CHECK(strcmp(fdt_strerror(-FDT_ERR_NOTFOUND), "FDT_ERR_NOTFOUND") == 0);
	CHECK(strcmp(fdt_strerror(0), "<no error>") == 0);
}

/*
 * cpp must run with -undef: a cpp that predefines "linux" (the host's)
 * turns "linux,code" into "1,code" (bsd.dtb.mk, DTSCPPFLAGS).
 */
TEST(cpp_undef)
{
	int keys, node, n = 0;

	load();
	REQUIRE((keys = fdt_node_offset_by_compatible(fdt, -1, "gpio-keys")) >= 0);
	fdt_for_each_subnode(node, fdt, keys) {
		CHECK(fdt_getprop(fdt, node, "linux,code", NULL) != NULL);
		CHECK(fdt_getprop(fdt, node, "1,code", NULL) == NULL);
		n++;
	}
	CHECK(n > 0);
}
