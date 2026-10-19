/*
 * Host-side check of the FDT parser against the lemonadep platform dtb:
 *   gcc -O1 -Wall -I. -o out/host_fdt host_fdt.c fdt.c && ./out/host_fdt /home/venus/dtb
 * Asserts the values this board is known to have (splash, ufshc, bootargs).
 */
#include <stdio.h>
#include <stdlib.h>
#include "fdt.h"

static unsigned char *blob;
static int fails;

static void check(const char *what, unsigned long got, unsigned long want)
{
	if (got != want) {
		printf("FAIL %s: got %#lx want %#lx\n", what, got, want);
		fails++;
	}
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	long n;
	unsigned long splash = 0, splash_sz = 0, ufs = 0, banks = 0;
	int off, c;

	if (!f) {
		perror(argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	blob = malloc(n);
	if (fread(blob, 1, n, f) != (size_t)n)
		return 2;
	fclose(f);

	if (!fdt_path((uintptr_t)blob, "/reserved-memory", &off)) {
		printf("FAIL no /reserved-memory\n");
		return 1;
	}
	c = FDT_NOFF;
	while (fdt_child((uintptr_t)blob, off, &c)) {
		uint64_t a, s;
		const char *lab = fdt_str((uintptr_t)blob, c, "label");
		if (!fdt_reg((uintptr_t)blob, c, 0, &a, &s))
			continue;
		if (lab && lab[0] == 'c' && lab[1] == 'o') {
			splash = a;
			splash_sz = s;
		}
	}
	check("splash base", splash, 0xe1000000UL);
	check("splash size", splash_sz, 0x2300000UL);

	if (fdt_compatible((uintptr_t)blob, "qcom,ufshc", &off)) {
		uint64_t a = 0, s = 0;
		fdt_reg1((uintptr_t)blob, off, 0, &a, &s);
		ufs = a;
	}
	check("ufshc base", ufs, 0x1d84000UL);

	if (!fdt_path((uintptr_t)blob, "/chosen", &off) || !fdt_str((uintptr_t)blob, off, "bootargs")) {
		printf("FAIL no /chosen bootargs\n");
		fails++;
	}

	c = FDT_NOFF;
	while (fdt_child((uintptr_t)blob, 0, &c)) {
		const char *dt = fdt_str((uintptr_t)blob, c, "device_type");
		uint64_t a, s;
		if (!dt || dt[0] != 'm' || dt[1] != 'e')
			continue;
		for (unsigned i = 0; i < 4 && fdt_reg((uintptr_t)blob, c, i, &a, &s); i++)
			banks++;
	}
	printf("memory banks in dtb: %lu\n", banks);
	printf("%s\n", fails ? "FAILED" : "ok");
	return fails != 0;
}
