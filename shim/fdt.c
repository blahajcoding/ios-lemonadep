/*
 * Just enough flattened-device-tree parsing to read what ABL left in the
 * blob: /memory banks, /reserved-memory, /chosen, and named peripherals.
 * Big-endian, 4-byte aligned tokens; byte loads for strings are legal on the
 * Device memory we see with the MMU off.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

#define MAX_DEPTH	16

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_name(uintptr_t p)
{
	while (*(volatile char *)p)
		p++;
	return align4(p + 1);
}

static int slen(const char *s)
{
	int n = 0;
	while (s[n])
		n++;
	return n;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int strings(uintptr_t fdt) { return be32(fdt + 12); }
/* Node offsets are relative to the structure block, not to the blob. */
static uintptr_t sblock(uintptr_t fdt) { return fdt + be32(fdt + 8); }

/* Offset of the token after the node that starts at the BEGIN_NODE at off. */
static int skip_node(uintptr_t fdt, int off)
{
	uintptr_t p = skip_name(sblock(fdt) + off + 4);
	int depth = 1;

	while (depth) {
		uint32_t t = be32(p);
		p += 4;
		switch (t) {
		case FDT_BEGIN_NODE:
			p = skip_name(p);
			depth++;
			break;
		case FDT_END_NODE:
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p);
			p = align4(p + 8 + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return FDT_NOFF;
		}
	}
	return (int)(p - sblock(fdt));
}

const char *fdt_node_name(uintptr_t fdt, int off)
{
	if (!fdt || be32(fdt) != FDT_MAGIC || off < 0)
		return "";
	return (const char *)(sblock(fdt) + off + 4);
}

const void *fdt_prop(uintptr_t fdt, int off, const char *name, uint32_t *len)
{
	if (off < 0)
		return 0;
	uintptr_t str = fdt + strings(fdt);
	uintptr_t p = skip_name(sblock(fdt) + off + 4);

	for (;;) {
		uint32_t t = be32(p);
		p += 4;
		if (t == FDT_END_NODE || t == FDT_END)
			return 0;
		if (t == FDT_NOP)
			continue;
		if (t != FDT_PROP)
			return 0;
		uint32_t l = be32(p), nameoff = be32(p + 4);
		uintptr_t val = p + 8;
		if (streq((const char *)(str + nameoff), name)) {
			if (len)
				*len = l;
			return (const void *)val;
		}
		p = align4(val + l);
	}
}

int fdt_u32(uintptr_t fdt, int off, const char *name, uint32_t *out)
{
	uint32_t len;
	const void *v = fdt_prop(fdt, off, name, &len);
	if (!v || len != 4)
		return 0;
	*out = be32((uintptr_t)v);
	return 1;
}

const char *fdt_str(uintptr_t fdt, int off, const char *name)
{
	const void *v = fdt_prop(fdt, off, name, 0);
	return v ? (const char *)v : 0;
}

int fdt_reg(uintptr_t fdt, int off, unsigned idx, uint64_t *addr, uint64_t *size)
{
	uint32_t len;
	const void *v = fdt_prop(fdt, off, "reg", &len);

	/* shortcut: 2 address + 2 size cells, which is what every node on this
	 * platform uses; revisit if a board needs per-parent cell counts. */
	if (!v || len < (idx + 1) * 16)
		return 0;
	uintptr_t p = (uintptr_t)v + idx * 16;
	*addr = (uint64_t)be32(p) << 32 | be32(p + 4);
	*size = (uint64_t)be32(p + 8) << 32 | be32(p + 12);
	return 1;
}

int fdt_reg1(uintptr_t fdt, int off, unsigned idx, uint64_t *addr, uint64_t *size)
{
	uint32_t len;
	const void *v = fdt_prop(fdt, off, "reg", &len);

	if (!v || len < (idx + 1) * 8)
		return 0;
	uintptr_t p = (uintptr_t)v + idx * 8;
	*addr = be32(p);
	*size = be32(p + 4);
	return 1;
}

/* First child of parent; pass *child = FDT_NOFF to start. Returns 0 at end. */
int fdt_child(uintptr_t fdt, int parent, int *child)
{
	uintptr_t p;

	if (*child < 0) {
		p = skip_name(sblock(fdt) + parent + 4);
	} else {
		int next = skip_node(fdt, *child);
		if (next < 0)
			return 0;
		p = sblock(fdt) + next;
	}
	for (;;) {
		uint32_t t = be32(p);
		if (t == FDT_END_NODE || t == FDT_END)
			return 0;
		if (t == FDT_NOP) {
			p += 4;
			continue;
		}
		if (t == FDT_PROP) {	/* a property of the parent node */
			uint32_t len = be32(p + 4);
			p = align4(p + 12 + len);	/* token, len, nameoff, value */
			continue;
		}
		if (t != FDT_BEGIN_NODE)
			return 0;
		*child = (int)(p - sblock(fdt));
		return 1;
	}
}

int fdt_path(uintptr_t fdt, const char *path, int *off)
{
	int cur = 0;	/* root node */
	if (!fdt || be32(fdt) != FDT_MAGIC || path[0] != '/')
		return 0;
	while (*path == '/')
		path++;
	while (*path) {
		char name[64];
		unsigned n = 0;
		while (*path && *path != '/' && n + 1 < sizeof(name))
			name[n++] = *path++;
		name[n] = 0;
		while (*path == '/')
			path++;
		int child = FDT_NOFF, found = 0;
		while (fdt_child(fdt, cur, &child))
			if (streq(fdt_node_name(fdt, child), name)) {
				cur = child;
				found = 1;
				break;
			}
		if (!found)
			return 0;
	}
	*off = cur;
	return 1;
}

static int compat_at(uintptr_t fdt, int off, const char *want)
{
	uint32_t len;
	const char *v = fdt_prop(fdt, off, "compatible", &len);
	if (!v)
		return 0;
	for (uint32_t i = 0; i < len; i += (uint32_t)(uint32_t)slen(v + i) + 1)
		if (streq(v + i, want))
			return 1;
	return 0;
}

static int find_compat(uintptr_t fdt, int node, const char *want, int *out, int depth)
{
	if (compat_at(fdt, node, want)) {
		*out = node;
		return 1;
	}
	if (depth >= MAX_DEPTH)
		return 0;
	int child = FDT_NOFF;
	while (fdt_child(fdt, node, &child))
		if (find_compat(fdt, child, want, out, depth + 1))
			return 1;
	return 0;
}

int fdt_compatible(uintptr_t fdt, const char *compat, int *off)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	return find_compat(fdt, 0, compat, off, 0);
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	int off;
	uint32_t len;
	if (!fdt_path(fdt, "/chosen", &off))
		return 0;
	const void *v = fdt_prop(fdt, off, prop, &len);
	if (!v)
		return 0;
	if (len == 4)
		*out = be32((uintptr_t)v);
	else if (len == 8)
		*out = (uint64_t)be32((uintptr_t)v) << 32 | be32((uintptr_t)v + 4);
	else
		return 0;
	return 1;
}
