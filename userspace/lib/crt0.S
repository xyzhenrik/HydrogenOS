    .intel_syntax noprefix

    .section .text
    .globl _start
    .extern main
    .extern environ_init
    .extern exit

# Entry registers (see sys_spawn_args): rdi = argc, rsi = argv, rdx = envp.
# r12-r14 carry them across the libc call; nothing has been pushed yet, so
# the stack alignment main sees is unchanged.
_start:
    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov rdi, rdx
    call environ_init
    mov rdi, r12
    mov rsi, r13
    mov rdx, r14
    call main
    mov edi, eax
    call exit

1:
    hlt
    jmp 1b

    .section .note.GNU-stack,"",@progbits
