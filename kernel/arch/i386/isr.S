.intel_syntax noprefix
.globl isr_default
isr_default:
    cli
halt_loop:
    hlt
    jmp halt_loop

    .section .note.GNU-stack,"",@progbits
