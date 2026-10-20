#!/usr/bin/env python3
"""Apply the lemonadep (OnePlus 9 Pro, SM8350) board overlay to a tree that
already has xnu/patches/0001-0025 applied.

Everything here is mechanical: widen the sweet-only guards so both boards share
one code path, and replace the constants that are genuinely per-board (early
framebuffer, timer PPI, platform name) with names defined in the board header.
Run from the tree root:  python3 tools/lemonadep_overlay.py .
"""
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."

SOURCES = (".c", ".h", ".cpp", ".s", ".def", "Makefile")

# vm_unix.c needs struct pid_hibernate_args, which only the macOS 13.5 config's
# SDK headers provide; skip it so patch 0070 stays sweet-only and syscall 435
# remains nosys for lemonadep until the userland milestone needs it.
SKIP = {"bsd/vm/vm_unix.c"}

# Order matters: the negated form first, then the plain one.
GUARDS = [
    ("defined(ARM64_BOARD_CONFIG_SWEET)",
     "(defined(ARM64_BOARD_CONFIG_SWEET) || defined(ARM64_BOARD_CONFIG_LEMONADEP))"),
    ("defined(SWEET)", "(defined(SWEET) || defined(LEMONADEP))"),
    ("defined (SWEET)", "(defined (SWEET) || defined (LEMONADEP))"),
    # Per-board values that live behind the shared guards.
    ("SWEET_VTIMER_INTID", "BOARD_VTIMER_INTID"),
    ("SWEET_IPI_SGI", "BOARD_IPI_SGI"),
]

FB_EDITS = {
    "osfmk/arm64/sweet_early.c": [
        ("(uintptr_t)0x9c000000", "(uintptr_t)BOARD_FB_BASE"),
        ("#define EARLY_STRIDE (4320u / 4)", "#define EARLY_STRIDE (BOARD_FB_STRIDE / 4)"),
    ],
    "osfmk/arm64/arm_vm_init.c": [
        ("*slot = 0x9c000000ULL", "*slot = (uint64_t)BOARD_FB_BASE"),
    ],
    "osfmk/arm64/start.s": [
        ("MOV64\tx0, (0x9c000000 + 2368 * 4320)", "MOV64\tx0, (BOARD_FB_BASE + BOARD_FB_END)"),
        ("mov\t\tx4, #4320", "mov\t\tx4, #BOARD_FB_STRIDE"),
        ("MOV64\tx10, 0x9c000000", "MOV64\tx10, BOARD_FB_BASE"),
    ],
    "iokit/Kernel/arm/SweetPlatformExpert.cpp": [
        ('strlcpy(name, "sweet", (size_t)maxLength);',
         'strlcpy(name, BOARD_NAME, (size_t)maxLength);'),
    ],
}

BOARD_HEADER = '''
/*
 * OnePlus 9 Pro ("lemonadep", Qualcomm SM8350 / Snapdragon 888). Shares the
 * sweet code path: ARMv8.2 without PAC, 16K pages, GICv3 whose Group 0 is
 * TrustZone's, so every interrupt arrives as a Group 1 IRQ.
 *
 * Board numbers measured from the live FDT/ABL handoff, not from a dts:
 *   splash_region 0xE4D00000 (live con.fb), ABL draws 1080x2400 with a 4320-byte stride
 *   virtual timer PPI 11 (DT <1 11 ...>) -> INTID 16 + 11 = 27
 */
'''.lstrip("\n")


def edit(path, subs):
    p = os.path.join(ROOT, path)
    s = open(p).read()
    for old, new in subs:
        s = s.replace(old, new)
    open(p, "w").write(s)


def main():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        if ".git" in dirnames:
            dirnames.remove(".git")
        for fn in filenames:
            if not fn.endswith(SOURCES) and fn != "Makefile":
                continue
            p = os.path.join(dirpath, fn)
            if os.path.relpath(p, ROOT) in SKIP:
                continue
            try:
                s = open(p).read()
            except (UnicodeDecodeError, IsADirectoryError):
                continue
            o = s
            s = s.replace("!defined(SWEET)", "!@NEGSWEET@")
            for old, new in GUARDS:
                s = s.replace(old, new)
            s = s.replace("!@NEGSWEET@", "!(defined(SWEET) || defined(LEMONADEP))")
            if s != o:
                open(p, "w").write(s)

    for path, subs in FB_EDITS.items():
        edit(path, subs)

    makeinc = os.path.join(ROOT, "makedefs/MakeInc.def")
    s = open(makeinc).read()
    s = s.replace("VMAPPLE SWEET", "VMAPPLE SWEET LEMONADEP")
    s = s.replace(
        "MACHINE_FLAGS_ARM64_SWEET = -DARM64_BOARD_CONFIG_SWEET                -march=armv8.2-a+crypto",
        "MACHINE_FLAGS_ARM64_SWEET = -DARM64_BOARD_CONFIG_SWEET                -march=armv8.2-a+crypto\n"
        "MACHINE_FLAGS_ARM64_LEMONADEP = -DARM64_BOARD_CONFIG_LEMONADEP        -march=armv8.2-a+crypto")
    s = s.replace(
        "ifneq ($(CURRENT_MACHINE_CONFIG),SWEET)\n"
        "export ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG := arm64e\n"
        "endif",
        "ifneq ($(CURRENT_MACHINE_CONFIG),SWEET)\n"
        "ifneq ($(CURRENT_MACHINE_CONFIG),LEMONADEP)\n"
        "export ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG := arm64e\n"
        "endif\n"
        "endif")
    open(makeinc, "w").write(s)

    bc = os.path.join(ROOT, "pexpert/pexpert/arm64/board_config.h")
    s = open(bc).read()
    s = s.replace('''#endif  /* ARM64_BOARD_CONFIG_SWEET */''',
                  '''#endif  /* ARM64_BOARD_CONFIG_SWEET */

#ifdef ARM64_BOARD_CONFIG_LEMONADEP
#include <pexpert/arm64/LEMONADEP.h>

#define MAX_L2_CLINE                   6
#define MAX_CPUS                       8
#define MAX_CPU_CLUSTERS               1

#define CORE_NCTRS                     8 /* Placeholder; KPC is not enabled for this target */

#endif  /* ARM64_BOARD_CONFIG_LEMONADEP */''')
    open(bc, "w").write(s)

    pm = os.path.join(ROOT, "pexpert/pexpert/arm64/Makefile")
    s = open(pm).read()
    s = s.replace("\tSWEET.h", "\tSWEET.h \\\n\tLEMONADEP.h")
    open(pm, "w").write(s)

    # sweet keeps working: same names, sweet's numbers.
    sweet_path = os.path.join(ROOT, "pexpert/pexpert/arm64/SWEET.h")
    sweet = open(sweet_path).read()
    sweet = sweet.replace("#define SWEET                     1",
                          "#define SWEET                     1\n\n"
                          "/* Board numbers the shared code reads by name. */\n"
                          "#define BOARD_FB_BASE             0x9c000000\n"
                          "#define BOARD_FB_END              10229760\n"
                          "#define BOARD_FB_STRIDE           4320\n"
                          '#define BOARD_NAME                "sweet"')
    sweet = sweet.replace("#define SWEET_VTIMER_INTID        19", "#define BOARD_VTIMER_INTID        19")
    sweet = sweet.replace("#define SWEET_IPI_SGI             1", "#define BOARD_IPI_SGI             1")
    open(sweet_path, "w").write(sweet)

    # lemonadep: same file, per-board numbers swapped.
    lem = BOARD_HEADER + sweet
    start = lem.index("/*\n * Board config for the Redmi")
    lem = lem[:start] + lem[lem.index("#ifndef _PEXPERT_ARM64_SWEET_H"):]
    lem = lem.replace("_PEXPERT_ARM64_SWEET_H", "_PEXPERT_ARM64_LEMONADEP_H")
    timer_old = """/*
 * Qualcomm numbers the architected timer PPIs 1/2/3/0 (secure phys, phys,
 * virt, hyp) instead of the usual 13/14/11/10: the virtual timer XNU uses
 * (CNTV) is INTID 16 + 3 = 19.
 */"""
    timer_new = """/*
 * The DT lists the architected timer as <1 13>, <1 14>, <1 11>, <1 12>:
 * the virtual timer XNU uses (CNTV) is PPI 11, i.e. INTID 16 + 11 = 27.
 * (sweet's dtb numbered the same PPIs 3 and landed on 19 - do not copy it.)
 */"""
    assert timer_old in lem
    lem = lem.replace(timer_old, timer_new)
    lem = lem.replace("#define SWEET                     1", "#define LEMONADEP                 1")
    lem = lem.replace("#define BOARD_FB_BASE             0x9c000000", "#define BOARD_FB_BASE             0xE4D00000")
    lem = lem.replace('#define BOARD_NAME                "sweet"', '#define BOARD_NAME                "lemonadep"')
    lem = lem.replace("#define BOARD_VTIMER_INTID        19", "#define BOARD_VTIMER_INTID        27")
    open(os.path.join(ROOT, "pexpert/pexpert/arm64/LEMONADEP.h"), "w").write(lem)

    master = open(os.path.join(ROOT, "config/MASTER.arm64.sweet")).read()
    master = master.replace(
        "# sweet (Redmi Note 10 Pro, SM7150): MASTER.arm64.MacOSX plus the open-source",
        "# lemonadep (OnePlus 9 Pro, SM8350): sweet's config. MASTER.arm64.MacOSX plus the open-source")
    open(os.path.join(ROOT, "config/MASTER.arm64.lemonadep"), "w").write(master)

    # Panic text and the post-TTBR0 console alias are invisible once the
    # bootstrap tables are gone; keep a copy of every early panic in a fixed
    # buffer a host (QEMU) can read back by physical address, and probe the
    # kernel framebuffer alias before trusting it.
    early_path = os.path.join(ROOT, "osfmk/arm64/sweet_early.c")
    early = open(early_path).read()
    mailbox = """volatile char sweet_panic_mailbox[1024];
static unsigned int sweet_panic_mailbox_len;

static void
sweet_panic_mailbox_putc(char c, void *arg)
{
	(void)arg;
	if (sweet_panic_mailbox_len < sizeof(sweet_panic_mailbox) - 1) {
		sweet_panic_mailbox[sweet_panic_mailbox_len++] = c;
	}
}

static void
sweet_panic_mailbox_store(const char *fmt, va_list *args)
{
	va_list copy;

	va_copy(copy, *args);
	_doprnt(fmt, &copy, sweet_panic_mailbox_putc, 0);
	va_end(copy);
	sweet_panic_mailbox[sweet_panic_mailbox_len] = 0;
}

"""
    decl = "void\nsweet_early_panic(const char *fmt, va_list *args)\n"
    assert decl in early, "sweet_early_panic not found"
    early = early.replace(decl, mailbox + decl, 1)
    body = "\tearly_panicking = 1;\n"
    assert body in early
    early = early.replace(body, body + "\tsweet_panic_mailbox_store(fmt, args);\n", 1)
    guard_old = "\tif (early_disabled || early_panicking) {\n"
    assert guard_old in early, "panic guard not found"
    early = early.replace(
        guard_old,
        "\tif (early_panicking) {\n",
        1,
    )
    early = early.replace(
        "\tearly_panicking = 1;\n",
        "\tearly_panicking = 1;\n\t/* A panic must be visible even after the framebuffer console took over. */\n\tearly_disabled = 0;\n",
        1,
    )
    probe_old = "\tearly_fb = (volatile uint32_t *)(uintptr_t)base;\n"
    assert probe_old in early
    probe_new = (
        "\t{\n"
        "\t\tvolatile uint32_t *alias = (volatile uint32_t *)(uintptr_t)base;\n"
        "\t\t*alias = 0xa5a5c0deu;\n"
        "\t\t__asm__ volatile(\"dsb sy\" ::: \"memory\");\n"
        "\t\tsweet_early_string(*alias == 0xa5a5c0deu ? \"ALIAS ok\\n\" : \"ALIAS bad\\n\");\n"
        "\t}\n"
    )
    early = early.replace(probe_old, probe_new + probe_old, 1)
    open(early_path, "w").write(early)


if __name__ == "__main__":
    main()
