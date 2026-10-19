#pragma once
#include <stdint.h>

/*
 * Minimal flattened-device-tree access. Node handles are byte offsets of the
 * FDT_BEGIN_NODE token inside the blob; -1 means "not found".
 * Everything here is safe with the MMU off (aligned 32/64-bit loads only).
 */
#define FDT_NOFF	(-1)

int fdt_path(uintptr_t fdt, const char *path, int *off);
int fdt_compatible(uintptr_t fdt, const char *compat, int *off);
int fdt_child(uintptr_t fdt, int parent, int *child);
const char *fdt_node_name(uintptr_t fdt, int off);
const void *fdt_prop(uintptr_t fdt, int off, const char *name, uint32_t *len);
int fdt_u32(uintptr_t fdt, int off, const char *name, uint32_t *out);
const char *fdt_str(uintptr_t fdt, int off, const char *name);
/* reg[idx] as (addr, size). Root-level nodes use 2/2 cells, /soc children 1/1. */
int fdt_reg(uintptr_t fdt, int off, unsigned idx, uint64_t *addr, uint64_t *size);
int fdt_reg1(uintptr_t fdt, int off, unsigned idx, uint64_t *addr, uint64_t *size);
/* Read a u32/u64 property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
