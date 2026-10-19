#pragma once
#include <stdint.h>

/* Defaults: reserved-memory cont_splash_region on sweet. con_init() replaces
 * them with the geometry the board's own FDT reports. */
#define FB_BASE		0x9c000000UL
#define FB_W		1080
#define FB_H		2400
#define FB_STRIDE	(FB_W * 4)	/* a8r8g8b8, matches mainline simplefb */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

struct fbcon {
	volatile uint32_t *fb;
	uint32_t w, h, pitch;	/* pitch in bytes */
};

extern struct fbcon con;

/* Point the console at a framebuffer; base 0 keeps the compile-time default. */
void con_init(uintptr_t base, uint32_t w, uint32_t h, uint32_t pitch);
/* No framebuffer known: discard all console output instead of writing blind. */
void con_disable(void);
void con_clear(uint32_t bg);
/* Clear and restart at the top: multi-page diagnostics. */
void con_page(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
