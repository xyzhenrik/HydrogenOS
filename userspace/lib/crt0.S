    .intel_syntax noprefix

    .section .text
    .globl _start
    .extern main

_start:
    call main
    mov edi, eax
    mov eax, 0
    syscall

1:
    hlt
    jmp 1b

    .section .note.GNU-stack,"",@progbits
