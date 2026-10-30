#include "pmm.h"
#include "multiboot.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
extern uint64_t _boot_start;
extern uint64_t _kernel_lma_end;
#else
extern uint32_t _kernel_start;
extern uint32_t _kernel_end;
#endif

#define PMM_PAGE_SIZE 4096
#define PMM_MAX_PAGES 32768 // bis 128 MiB; reicht für frühe Phasen

static uint8_t page_bitmap[PMM_MAX_PAGES];
/* Invariant: no page below this index is free, so allocation scans start here
   instead of at page 0 once the low memory fills up. */
static size_t first_free_hint;

static inline void set_bit(size_t idx) {
  page_bitmap[idx / 8] |= (1u << (idx % 8));
}
static inline void clear_bit(size_t idx) {
  page_bitmap[idx / 8] &= ~(1u << (idx % 8));
}
static inline int test_bit(size_t idx) {
  return page_bitmap[idx / 8] & (1u << (idx % 8));
}

static void reserve_region(uintptr_t start, uintptr_t end) {
  size_t first = (size_t)(start / PMM_PAGE_SIZE);
  size_t last = (size_t)((end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);
  if (last > PMM_MAX_PAGES)
    last = PMM_MAX_PAGES;
  for (size_t i = first; i < last; ++i) {
    set_bit(i);
  }
}

void pmm_init(uintptr_t multiboot_info_addr) {
  // Alle Seiten zunächst reservieren.
  for (size_t i = 0; i < sizeof(page_bitmap); ++i) {
    page_bitmap[i] = 0xFF;
  }

  multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)multiboot_info_addr;
  if (mbi && (mbi->flags & MULTIBOOT_FLAG_MMAP)) { // mmap valid
    uintptr_t mmap_end =
        (uintptr_t)mbi->mmap_addr + (uintptr_t)mbi->mmap_length;
    for (uintptr_t p = (uintptr_t)mbi->mmap_addr; p < mmap_end;) {
      multiboot_mmap_entry_t *e = (multiboot_mmap_entry_t *)p;
      if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
        uint64_t start = e->addr;
        uint64_t end = e->addr + e->len;
        if (start < (uint64_t)PMM_MAX_PAGES * PMM_PAGE_SIZE) {
          if (end > (uint64_t)PMM_MAX_PAGES * PMM_PAGE_SIZE) {
            end = (uint64_t)PMM_MAX_PAGES * PMM_PAGE_SIZE;
          }
          size_t first = (size_t)(start / PMM_PAGE_SIZE);
          size_t last = (size_t)((end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);
          for (size_t i = first; i < last && i < PMM_MAX_PAGES; ++i) {
            clear_bit(i);
          }
        }
      }
      p += (uintptr_t)e->size + sizeof(e->size);
    }
  }

  // Reserviere unter 1 MiB und Kernel/Boot-Bereich.
  reserve_region(0, 0x00100000);
#if defined(__x86_64__)
  reserve_region((uintptr_t)&_boot_start, (uintptr_t)&_kernel_lma_end);
#else
  reserve_region((uintptr_t)&_kernel_start, (uintptr_t)&_kernel_end);
#endif

  if (mbi && (mbi->flags & MULTIBOOT_FLAG_MODS) && mbi->mods_count > 0) {
    multiboot_module_t *mods =
        (multiboot_module_t *)(uintptr_t)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; ++i) {
      if (mods[i].mod_end > mods[i].mod_start) {
        reserve_region(mods[i].mod_start, mods[i].mod_end);
      }
    }
  }
}

uintptr_t pmm_alloc_page(void) {
  for (size_t i = first_free_hint; i < PMM_MAX_PAGES; ++i) {
    if (!test_bit(i)) {
      set_bit(i);
      first_free_hint = i + 1;
      return (uintptr_t)(i * PMM_PAGE_SIZE);
    }
  }
  first_free_hint = PMM_MAX_PAGES;
  return 0; // out of memory
}

uintptr_t pmm_alloc_contiguous(size_t pages) {
  if (pages == 0 || pages > PMM_MAX_PAGES) {
    return 0;
  }
  size_t run_start = 0;
  size_t run_len = 0;
  for (size_t i = 0; i < PMM_MAX_PAGES; ++i) {
    if (test_bit(i)) {
      run_len = 0;
      continue;
    }
    if (run_len == 0) {
      run_start = i;
    }
    run_len++;
    if (run_len == pages) {
      for (size_t j = 0; j < pages; ++j) {
        set_bit(run_start + j);
      }
      return (uintptr_t)(run_start * PMM_PAGE_SIZE);
    }
  }
  return 0;
}

void pmm_free_page(uintptr_t addr) {
  size_t index = (size_t)(addr / PMM_PAGE_SIZE);
  if (index < PMM_MAX_PAGES) {
    clear_bit(index);
    if (index < first_free_hint)
      first_free_hint = index;
  }
}

uintptr_t pmm_first_free_frame(void) {
  for (size_t i = first_free_hint; i < PMM_MAX_PAGES; ++i) {
    if (!test_bit(i)) {
      return (uintptr_t)i;
    }
  }
  return (uintptr_t)-1;
}

size_t pmm_free_page_count(void) {
  size_t n = 0;
  for (size_t i = 0; i < PMM_MAX_PAGES; ++i)
    if (!test_bit(i)) n++;
  return n;
}

size_t pmm_total_page_count(void) {
  return PMM_MAX_PAGES;
}
