    .section .text
    .globl _start
_start:
    mov $1, %rax
    mov $1, %rdi
    lea msg(%rip), %rsi
    mov $msg_len, %rdx
    syscall

    mov $0, %rax
    xor %rdi, %rdi
    syscall

    .section .rodata
msg:
    .ascii "Hello from user mode via sys_write!\n"
msg_len = . - msg

    .section .note.GNU-stack,"",@progbits
