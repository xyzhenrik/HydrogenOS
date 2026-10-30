#include "cmdline.h"

#include <stddef.h>

#include "multiboot.h"

#define CMDLINE_CAP 256
/* The early page tables identity-map the first GiB only. */
#define CMDLINE_ADDRESS_LIMIT 0x40000000u

static char cmdline[CMDLINE_CAP];

void cmdline_init(uintptr_t multiboot_info_addr) {
  const multiboot_info_t *mbi =
      (const multiboot_info_t *)multiboot_info_addr;
  cmdline[0] = '\0';
  if (!mbi || !(mbi->flags & MULTIBOOT_FLAG_CMDLINE) || mbi->cmdline == 0 ||
      mbi->cmdline >= CMDLINE_ADDRESS_LIMIT)
    return;
  const char *src = (const char *)(uintptr_t)mbi->cmdline;
  size_t i = 0;
  for (; i < CMDLINE_CAP - 1 && src[i]; ++i)
    cmdline[i] = src[i];
  cmdline[i] = '\0';
}

int cmdline_has_flag(const char *name) {
  size_t i = 0;
  while (cmdline[i]) {
    while (cmdline[i] == ' ')
      ++i;
    size_t start = i;
    while (cmdline[i] && cmdline[i] != ' ')
      ++i;
    size_t j = 0;
    while (start + j < i && name[j] && cmdline[start + j] == name[j])
      ++j;
    if (j > 0 && start + j == i && name[j] == '\0')
      return 1;
  }
  return 0;
}

const char *cmdline_text(void) {
  return cmdline;
}
