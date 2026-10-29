#include "power.h"

#include <stdint.h>

#include "acpi.h"
#include "drivers/block_cache.h"
#include "errno.h"
#include "io.h"
#include "serial.h"

#define KBC_STATUS_PORT 0x64
#define KBC_STATUS_INPUT_FULL 0x02
#define KBC_COMMAND_PULSE_RESET 0xFE
/* Bounded in port 0x80 writes (~1 us each); a missing 8042 floats to 0xFF. */
#define KBC_READY_TIMEOUT_US 100000U
#define RESET_SETTLE_US 500000U

static void power_delay_us(uint32_t us) {
  while (us-- > 0)
    io_wait();
}

static uint64_t power_irq_save(void) {
  uint64_t flags;
  __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
  return flags;
}

static void power_irq_restore(uint64_t flags) {
  __asm__ volatile("push %0\n\tpopfq" : : "r"(flags) : "memory", "cc");
}

static void power_keyboard_controller_reset(void) {
  for (uint32_t waited = 0; waited < KBC_READY_TIMEOUT_US; ++waited) {
    if ((inb(KBC_STATUS_PORT) & KBC_STATUS_INPUT_FULL) == 0)
      break;
    io_wait();
  }
  outb(KBC_STATUS_PORT, KBC_COMMAND_PULSE_RESET);
  power_delay_us(RESET_SETTLE_US);
}

static __attribute__((noreturn)) void power_triple_fault(void) {
  static const struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
  } empty_idt = {0, 0};
  __asm__ volatile("lidt %0\n\tint3" : : "m"(empty_idt) : "memory");
  for (;;)
    __asm__ volatile("cli\n\thlt");
}

int power_shutdown(void) {
  if (!acpi_s5_available())
    return -ENOSYS;
  if (block_cache_sync() != 0)
    return -EIO;
  serial_write("[POWER] entering S5\n");
  uint64_t flags = power_irq_save();
  acpi_enter_s5();
  power_irq_restore(flags);
  serial_write("[POWER] S5 entry failed\n");
  return -EIO;
}

int power_reboot(void) {
  if (block_cache_sync() != 0)
    return -EIO;
  serial_write("[POWER] rebooting\n");
  power_irq_save();
  acpi_reset();
  power_keyboard_controller_reset();
  power_triple_fault();
}
