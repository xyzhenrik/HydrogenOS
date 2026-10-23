.intel_syntax noprefix

.global gdt_flush

# gdt_flush(uint32_t ptr)
# ptr points to struct { uint16_t limit; uint32_t base; }

gdt_flush:
    lgdt [esp + 4]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:flush_next
flush_next:
    ret

    .section .note.GNU-stack,"",@progbits
