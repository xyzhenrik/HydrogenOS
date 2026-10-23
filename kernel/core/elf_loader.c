#include "elf64.h"
#include "console.h"
#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

int elf64_validate(const void *image, size_t size) {
    if (!image || size < sizeof(elf64_ehdr_t))
        return -1;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    if (eh->e_magic != ELF64_MAGIC)
        return -2;
    if (eh->e_class != 2) // ELFCLASS64
        return -3;
    if (eh->e_phoff == 0 || eh->e_phnum == 0)
        return -4;
    return 0;
}

void elf64_dump_info(const void *image) {
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    console_write("ELF64 entry: ");
    console_write_hex64(eh->e_entry);
    console_write("\n");
}

int elf64_load_image(const void *image, size_t size) {
    if (elf64_validate(image, size) != 0)
        return -1;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    const elf64_phdr_t *ph = (const elf64_phdr_t *)((const uint8_t *)image + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != ELF64_PT_LOAD)
            continue;
        uintptr_t vaddr = (uintptr_t)ph[i].p_vaddr;
        uintptr_t seg_base = vaddr & ~0xFFFULL;
        uintptr_t seg_off = vaddr - seg_base;
        uintptr_t memsz = (uintptr_t)ph[i].p_memsz;
        uintptr_t filesz = (uintptr_t)ph[i].p_filesz;
        if (memsz == 0)
            continue;

        size_t pages = (size_t)((seg_off + memsz + 0xFFF) / 0x1000);
        for (size_t p = 0; p < pages; ++p) {
            uintptr_t page_virt = seg_base + p * 0x1000;
            if (paging_is_mapped(page_virt))
                continue;
            uintptr_t phys = pmm_alloc_page();
            if (!phys)
                return -2;
            paging_map_region(phys, page_virt, 0x1000, PAGE_RW | PAGE_USER);
            uint8_t *page = (uint8_t *)(uintptr_t)page_virt;
            for (size_t j = 0; j < 0x1000; ++j) {
                page[j] = 0;
            }
        }

        const uint8_t *src = (const uint8_t *)image + ph[i].p_offset;
        uint8_t *dst = (uint8_t *)(uintptr_t)vaddr;
        for (uintptr_t b = 0; b < filesz; ++b) {
            dst[b] = src[b];
        }
        for (uintptr_t b = filesz; b < memsz; ++b) {
            dst[b] = 0;
        }
    }
    return 0;
}
