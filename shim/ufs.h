#pragma once
#include <stdint.h>

/* Probe the UFS host at base (read-only SCSI on LUN 0). */
void test_ufs(uintptr_t base);

/* READ CAPACITY on LUN 0-7: the GPT says userdata is ~78 GiB, so one LU is
 * not the whole device and LUN0 capacity alone cannot validate the layout. */
void ufs_luns(void);
