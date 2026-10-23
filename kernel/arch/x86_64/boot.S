/* Multiboot1 header and entry for x86_64 long mode */
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

    .section .text.boot, "ax"
    .code32
    .globl _start
_start:
    cli
    mov $stack_top32, %esp
    mov %eax, multiboot_magic
    mov %ebx, multiboot_info

    call serial_init_early

    call setup_page_tables

    lgdt gdt_descriptor

    mov %cr4, %eax
    or $0x20, %eax        # enable PAE
    mov %eax, %cr4

    mov $pml4, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx # EFER
    rdmsr
    or $0x00000100, %eax  # LME
    wrmsr

    mov %cr0, %eax
    or $0x80000000, %eax  # enable paging
    mov %eax, %cr0

    ljmp $0x08, $long_mode_entry

setup_page_tables:
    mov $pml4, %edi
    xor %eax, %eax
    mov $3072, %ecx
    rep stosl

    mov $pdpt, %eax
    or $0x003, %eax
    mov %eax, pml4
    movl $0, pml4+4

    mov $pd, %eax
    or $0x003, %eax
    mov %eax, pdpt
    movl $0, pdpt+4

    mov $pd, %edi
    xor %ebx, %ebx
    mov $512, %ecx
pd_loop:
    mov %ebx, %eax
    or $0x083, %eax       # present|rw|ps(2MiB)
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $8, %edi
    add $0x200000, %ebx
    loop pd_loop

    # map higher half: PML4[256] -> same PDPT (0xFFFF8000_0000_0000)
    mov $pdpt, %eax
    or $0x003, %eax
    mov %eax, pml4 + (256 * 8)
    movl $0, pml4 + (256 * 8) + 4
    ret

    .code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    # enable SSE (CR0.MP=1, CR0.EM=0, CR0.TS=0; CR4.OSFXSR=1, CR4.OSXMMEXCPT=1)
    mov %cr0, %rax
    and $~(1 << 2), %rax
    and $~(1 << 3), %rax
    or $(1 << 1), %rax
    mov %rax, %cr0
    mov %cr4, %rax
    or $((1 << 9) | (1 << 10)), %rax
    mov %rax, %cr4

    mov $stack_top64, %rsp
    sub $8, %rsp  # align stack for SysV ABI (as if return address were pushed)
    mov multiboot_magic(%rip), %edi
    mov multiboot_info(%rip), %esi
    xor %ebp, %ebp
    movabs $kmain64, %rax
    jmp *%rax

halt:
    hlt
    jmp halt

serial_init_early:
    mov $0x00, %al
    mov $0x3F9, %dx
    out %al, %dx
    mov $0x80, %al
    mov $0x3FB, %dx
    out %al, %dx
    mov $0x03, %al
    mov $0x3F8, %dx
    out %al, %dx
    mov $0x00, %al
    mov $0x3F9, %dx
    out %al, %dx
    mov $0x03, %al
    mov $0x3FB, %dx
    out %al, %dx
    mov $0xC7, %al
    mov $0x3FA, %dx
    out %al, %dx
    mov $0x0B, %al
    mov $0x3FC, %dx
    out %al, %dx
    ret

    .section .rodata.boot
    .align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

    .section .bss.boot
    .align 4096
    .globl pml4
pml4:
    .skip 4096
    .globl pdpt
pdpt:
    .skip 4096
    .globl pd
pd:
    .skip 4096

    .align 4
multiboot_magic:
    .long 0
multiboot_info:
    .long 0

    .align 16
stack_bottom32:
    .skip 16384
stack_top32:

    .align 16
stack_bottom64:
    .skip 32768
    .globl stack_top64
stack_top64:

    .section .note.GNU-stack,"",@progbits
