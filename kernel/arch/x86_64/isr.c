#include "isr.h"
#include "apic.h"
#include "console.h"
#include "core/time.h"
#include "keyboard.h"
#include "mouse.h"
#include "panic.h"
#include "pic.h"
#include "serial.h"
#include "io.h"
#include "paging.h"
#include "core/process.h"
#include "syscall.h"

static const char *exception_messages[] = {
    "Division by Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

static uint64_t irq0_ticks = 0;

/*
 * IRQs are still delivered through the 8259 PIC, but the LAPIC is already
 * enabled. Acknowledge both controllers so the handler stays correct once the
 * IOAPIC takes over delivery; a spurious LAPIC/PIC EOI is a no-op.
 */
static void irq_eoi(uint8_t irq) {
    pic_send_eoi(irq);
    lapic_eoi();
}

regs_t *isr_handler(regs_t *r) {
    if (r->int_no == 128) {
        r->rax = syscall_handle(r);
        if (process_current() != process_kernel()) {
            regs_t *next = process_schedule(r);
            if (next != r) {
                paging_switch(process_current()->pml4);
            }
            return next;
        }
        return r;
    }
    if (r->int_no < 32) {
        console_write("EXCEPTION: ");
        console_write(exception_messages[r->int_no]);
        if (r->int_no == 14) {
            console_write(" CR2=");
            console_write_hex64(read_cr2());
            console_write(" FLAGS:");
            console_write_hex((uint32_t)r->err_code);
            console_write(" (P=");
            console_putc((r->err_code & 1) ? '1' : '0');
            console_write(" W=");
            console_putc((r->err_code & 2) ? '1' : '0');
            console_write(" U=");
            console_putc((r->err_code & 4) ? '1' : '0');
            console_write(")");
        }
        console_write("\nINT: ");
        console_write_hex((uint32_t)r->int_no);
        console_write("  ERR: ");
        console_write_hex((uint32_t)r->err_code);
        console_write("\nRIP: ");
        console_write_hex64(r->rip);
        console_write("  RSP: ");
        console_write_hex64(r->rsp);
        console_write("\n");
        PANIC("CPU EXCEPTION");
    }

    if (r->int_no >= 32 && r->int_no < 48) {
        uint8_t irq = (uint8_t)(r->int_no - 32);
        if (irq == 0) {
            time_on_tick();
            irq0_ticks++;
            /* Only a frame that interrupted user mode may become a process's
               saved frame; a kernel-mode frame has to resume where it is. */
            if ((r->cs & 3) == 3 && process_current() != process_kernel()) {
                regs_t *next = process_schedule(r);
                if (next != r) {
                    paging_switch(process_current()->pml4);
                    irq_eoi(irq);
                    return next;
                }
            }
            // Debug tick output disabled to avoid noisy serial interleaving.
        } else if (irq == 1) {
            uint8_t sc = inb(0x60);
            keyboard_handle_scancode(sc);
        } else if (irq == 12) {
            mouse_handle_irq();
        }
        irq_eoi(irq);
    }

    return r;
}
