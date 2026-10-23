#include "isr.h"
#include "console.h"
#include "pic.h"
#include "io.h"
#include "keyboard.h"
#include "core/time.h"
#include "core/task.h"
#include "mouse.h"
#include "panic.h"

static volatile uint32_t tick_count = 0;

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

regs_t *isr_handler(regs_t *r) {
    if (r->int_no < 32) {
        console_write("EXCEPTION: ");
        console_write(exception_messages[r->int_no]);
        if (r->int_no == 14) {
            console_write(" CR2=");
            console_write_hex(read_cr2());
            console_write(" FLAGS:");
            console_write_hex(r->err_code);
            console_write(" (P=");
            console_putc((r->err_code & 1) ? '1' : '0');
            console_write(" W=");
            console_putc((r->err_code & 2) ? '1' : '0');
            console_write(" U=");
            console_putc((r->err_code & 4) ? '1' : '0');
            console_write(")");
        }
        console_write("\nINT: ");
        console_write_hex(r->int_no);
        console_write("  ERR: ");
        console_write_hex(r->err_code);
        console_write("\nEIP: ");
        console_write_hex(r->eip);
        console_write("  ESP: ");
        console_write_hex(r->esp);
        console_write("\n");
        PANIC("CPU EXCEPTION");
    }

    if (r->int_no == 48) {
        return task_schedule(r);
    }

    // IRQs 32-47
    if (r->int_no >= 32 && r->int_no < 48) {
        uint8_t irq = (uint8_t)(r->int_no - 32);
        if (irq == 0) {
            time_on_tick();
            r = task_schedule(r);
        } else if (irq == 1) {
            uint8_t sc = inb(0x60);
            keyboard_handle_scancode(sc);
        } else if (irq == 12) {
            mouse_handle_irq();
        }
        pic_send_eoi(irq);
    }

    return r;
}
