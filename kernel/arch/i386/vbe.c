#include "vbe.h"
#include "io.h"
#include "console.h"

#define BIOS_DATA_SEG 0x2000
#define FAR_PTR(seg, off) ((void *)((((uint32_t)(seg)) << 4) + (off)))

uint32_t framebuffer_phys = 0;
uint16_t framebuffer_pitch = 0;
uint16_t framebuffer_width = 0;
uint16_t framebuffer_height = 0;
uint8_t framebuffer_bpp = 0;

static int bios_int10_ax(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx, void *esdi) {
    int result;
    __asm__ volatile (
        "push %%es\n\t"
        "mov %6, %%es\n\t"
        "mov %5, %%edi\n\t"
        "int $0x10\n\t"
        "setc %0\n\t"
        "pop %%es\n\t"
        : "=r"(result)
        : "a"(ax), "b"(bx), "c"(cx), "d"(dx), "D"((uint16_t)((uint32_t)esdi & 0xF)), "r"((uint16_t)((uint32_t)esdi >> 4))
        : "cc", "memory"
    );
    return result;
}

int vbe_find_mode(uint16_t desired_w, uint16_t desired_h, uint8_t desired_bpp, uint16_t *mode_out, vbe_mode_info_t *info_out) {
    // For brevity: choose common 1024x768x32 (0x118 or 0x115). We'll probe 0x118 then 0x115.
    uint16_t candidates[] = {0x118, 0x115, 0};
    for (int i = 0; candidates[i]; ++i) {
        uint16_t mode = candidates[i];
        vbe_mode_info_t *mi = (vbe_mode_info_t *)0x8000; // scratch area
        __asm__ volatile (
            "push %%es\n\t"
            "mov $0x800, %%ax\n\t"
            "mov %%ax, %%es\n\t"
            "mov %1, %%cx\n\t"
            "mov $0x4F01, %%ax\n\t"
            "mov $0x0000, %%di\n\t"
            "int $0x10\n\t"
            "pop %%es\n\t"
            :
            : "c"(mode)
            : "ax", "di", "memory"
        );
        *info_out = *mi;
        if (info_out->width == desired_w && info_out->height == desired_h && info_out->bpp == desired_bpp) {
            *mode_out = mode;
            return 0;
        }
    }
    return -1;
}

int vbe_set_mode(uint16_t mode) {
    // set mode with linear framebuffer (bit14)
    uint16_t bx = mode | (1 << 14);
    __asm__ volatile (
        "mov %0, %%bx\n\t"
        "mov $0x4F02, %%ax\n\t"
        "int $0x10\n\t"
        :
        : "r"(bx)
        : "ax", "bx", "memory"
    );
    return 0;
}
