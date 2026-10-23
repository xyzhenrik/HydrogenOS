#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

// Simple identity mapping der ersten 4 MiB mit 4 KiB Seiten.
// Nutzt statische Tabellen, damit wir nicht versehentlich in Kernel-Speicher
// allozieren, solange der PMM noch Platzhalter ist.

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));
static uint32_t *page_tables[1024] = {0};

static void load_page_directory(uint32_t phys_addr) {
  __asm__ volatile("mov %0, %%cr3" ::"r"(phys_addr) : "memory");
}

static void enable_paging(void) {
  uint32_t cr0;
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000; // set PG bit
  __asm__ volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
}

void paging_init(void) {
  // Zero page directory/table
  for (int i = 0; i < 1024; ++i) {
    page_directory[i] = 0;
    first_page_table[i] = 0;
  }

  // Identity map first 4 MiB
  for (int i = 0; i < 1024; ++i) {
    uint32_t phys = (uint32_t)(i * 0x1000);
    first_page_table[i] = phys | PAGE_PRESENT | PAGE_RW;
  }

  // Da der Kernel identity-gemappt liegt, können wir die statische Tabelle
  // direkt verwenden.
  uint32_t pt_phys = (uint32_t)(uintptr_t)first_page_table;
  page_directory[0] = pt_phys | PAGE_PRESENT | PAGE_RW;
  page_tables[0] = (uint32_t *)(uintptr_t)pt_phys;

  load_page_directory((uint32_t)(uintptr_t)page_directory);
  enable_paging();
}

void paging_map_region(uintptr_t phys, uintptr_t virt, size_t size_bytes,
                       uint32_t flags) {
  uintptr_t end = phys + size_bytes;
  for (uintptr_t addr = phys; addr < end; addr += 0x1000, virt += 0x1000) {
    uint32_t pd_idx = (uint32_t)(virt >> 22);
    uint32_t pt_idx = (uint32_t)((virt >> 12) & 0x3FF);
    uint32_t *pt = page_tables[pd_idx];
    if (!pt) {
      uint32_t new_pt_phys = (uint32_t)pmm_alloc_page();
      if (new_pt_phys == 0)
        return;
      pt = (uint32_t *)(uintptr_t)new_pt_phys;
      for (int i = 0; i < 1024; ++i)
        pt[i] = 0;
      page_directory[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_RW;
      page_tables[pd_idx] = pt;
    }
    pt[pt_idx] = addr | PAGE_PRESENT | PAGE_RW | (flags & 0xFFF);
    __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
  }
}

uintptr_t paging_alloc_pages(size_t pages, uint32_t flags) {
  uintptr_t first = 0;
  for (size_t i = 0; i < pages; ++i) {
    uintptr_t phys = pmm_alloc_page();
    if (!phys)
      return 0;
    if (i == 0)
      first = phys;
    paging_map_region(phys, phys, 4096, flags);
  }
  return first;
}

int paging_is_mapped(uintptr_t virt) {
  uint32_t pd_idx = (uint32_t)(virt >> 22);
  uint32_t pt_idx = (uint32_t)((virt >> 12) & 0x3FF);
  uint32_t *pt = page_tables[pd_idx];
  if (!pt)
    return 0;
  return (pt[pt_idx] & PAGE_PRESENT) != 0;
}
