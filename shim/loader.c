/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "mmu.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

extern void xnu_vec_trap(void);

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

/* Two PC-independent instructions so the copy runs from anywhere. */
static __attribute__((naked, noinline)) void exec_stub(void)
{
	__asm__ volatile("str x1, [x0]\n\tret\n\t");
}

typedef void (*stub_t)(volatile uint64_t *dst, uint64_t val);

/*
 * Prove that a copied routine runs from ordinary RAM far above 4 GiB, and
 * that the framebuffer is visible at row 1200 (not only at the top).
 */
static int exec_probe(void)
{
	uintptr_t dst_pa = 0x13FF00000UL;
	volatile uint64_t *fb =
	    (volatile uint64_t *)((uintptr_t)0xE4000000UL + 1200UL * 4320UL);

	*(volatile uint64_t *)dst_pa = ((const uint64_t *)(uintptr_t)exec_stub)[0];
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((stub_t)dst_pa)(fb, 0xF00DF00DF00DF00DUL);
	__asm__ volatile("dsb sy" ::: "memory");
	con_str("EXEC", *fb == 0xF00DF00DF00DF00DUL ? "OK" : "BAD");
	return *fb == 0xF00DF00DF00DF00DUL;
}

const char *loader_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no SWEETXNU payload";
	if (p->version != 1 || (p->adt_off & 7) || (rd_start & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */

	/* Prove high-memory execution; green = OK, orange = the stub never ran. */
	con_str("EXEC", exec_probe() ? "OK" : "BAD");
	{
		/* Does instruction fetch work at the kernel's own entry address? */
		uintptr_t ep = p->entry_va + pa_of_va_base;
		uint64_t saved = *(volatile uint64_t *)ep;
		volatile uint64_t *tgt =
		    (volatile uint64_t *)((uintptr_t)con.fb + 1500UL * 4320UL);
		*(volatile uint64_t *)0x13FF80000UL =
		    ((const uint64_t *)(uintptr_t)exec_stub)[0];
		*(volatile uint64_t *)ep = *(volatile uint64_t *)0x13FF80000UL;
		__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
		((stub_t)ep)(tgt, 0xABCDEF01ABCDEF01UL);
		__asm__ volatile("dsb sy" ::: "memory");
		con_str("ENTRY", *tgt == 0xABCDEF01ABCDEF01UL ? "OK" : "BAD");
		*(volatile uint64_t *)ep = saved;
		__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	}
	uint64_t sctlr_off;
	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr_off));
	con_hex("SCTLR", sctlr_off);
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = (uintptr_t)con.fb;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = con.pitch;
	ba->Video.v_width = con.w;
	ba->Video.v_height = con.h;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	/* $_LowExceptionVectorBase in this kernel build: virt 0xfffffe000748c000,
	 * i.e. 0x487c000 into __TEXT. Each 0x80-byte slot is "b ." in the source;
	 * replace it with movz/movk/br so faults land in xnu_vector_hit(slot). */
	{
		uint32_t *v = (uint32_t *)(p->phys_base + 0x487c000UL);
		uintptr_t t = (uintptr_t)&xnu_vec_trap;

		for (uint32_t i = 0; i < 16; i++) {
			uint32_t *e = v + (i * 0x80) / 4;
			e[0] = 0xd2800000U | (uint32_t)(t & 0xffff) << 5 | 9;
			e[1] = 0xf2800000U | 1U << 21 | (uint32_t)(t >> 16 & 0xffff) << 5 | 9;
			e[2] = 0xf2800000U | 2U << 21 | (uint32_t)(t >> 32 & 0xffff) << 5 | 9;
			e[3] = 0xf2800000U | 3U << 21 | (uint32_t)(t >> 48 & 0xffff) << 5 | 9;
			e[4] = 0xd2800000U | (i & 0xffff) << 5;
			e[5] = 0xd61f0120U;
		}
	}

	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* The arena is cacheable: push the image out and drop stale I-cache lines. */
	{
		uintptr_t lo = p->phys_base & ~63UL;
		uintptr_t hi = (p->kernel_end + pa_of_va_base + 63UL) & ~63UL;
		__asm__ volatile("dsb sy" ::: "memory");
		for (uintptr_t a = lo; a < hi; a += 64)
			__asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
		__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	}
	{
		/* Dark blue wash: any write from start.s is now unmistakable. */
		volatile uint32_t *f = (volatile uint32_t *)(uintptr_t)con.fb;
		for (uint32_t i = 0; i < 1080U * 2400U; i++)
			f[i] = 0x00102040U;
		__asm__ volatile("dsb sy" ::: "memory");
	}
	/* MMU stays on: with it off, ABL's TCR_EL1.IPS limits PAs to 32 bits and
	 * every access above 4 GiB takes an address-size abort. */
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
