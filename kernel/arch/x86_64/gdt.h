#pragma once

#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_DATA_SELECTOR 0x1B
#define GDT_USER_CODE_SELECTOR 0x23

void gdt_init(void);
