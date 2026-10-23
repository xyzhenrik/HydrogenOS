/* Multiboot1 header and entry */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

    .section .multiboot, "a"
    .align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM
    # no video request here

    .section .text
    .code32
.globl _start
_start:
    cli
    mov $stack_top, %esp
    # debug: port 0xE9
    mov $0xE9, %dx
    mov $'a', %al
    out %al, %dx
    push %ebx            # multiboot info pointer
    push %eax            # multiboot magic
    call kmain

halt:
    cli
    hlt
    jmp halt

    .section .bss
    .align 16
stack_bottom:
    .skip 16384
stack_top:

    .section .note.GNU-stack,"",@progbits

    # no Multiboot2 header
