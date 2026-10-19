#pragma once
#include <stdint.h>

/* Probe the UFS host at base (read-only SCSI on LUN 0). */
void test_ufs(uintptr_t base);
