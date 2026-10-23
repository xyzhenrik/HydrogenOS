    .intel_syntax noprefix

    .globl isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .globl isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .globl isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .globl isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .globl irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    .globl irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    .globl isr48
    .globl isr128
    .globl isr_stub_table

    .section .text

.globl isr_common_stub
isr_common_stub:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_handler
    mov rsp, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp, 16       # err_code + int_no
    iretq

.macro ISR_NOERR num
    .globl isr\num
isr\num:
    cli
    push 0
    push \num
    jmp isr_common_stub
.endm

.macro ISR_ERR num
    .globl isr\num
isr\num:
    cli
    push \num
    jmp isr_common_stub
.endm

.macro IRQ num, vec
    .globl irq\num
irq\num:
    cli
    push 0
    push \vec
    jmp isr_common_stub
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

.globl isr48
isr48:
    cli
    push 0
    push 48
    jmp isr_common_stub

.globl isr128
isr128:
    cli
    push 0
    push 128
    jmp isr_common_stub

isr_stub_table:
    .quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .quad isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .quad irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    .quad irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    .quad isr48

    .section .note.GNU-stack,"",@progbits
