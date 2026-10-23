    .intel_syntax noprefix

    .section .text
    .globl syscall_entry

    .extern isr_handler
    .extern stack_top64

syscall_entry:
    mov qword ptr [rip + syscall_user_rax], rax
    mov qword ptr [rip + syscall_user_rsp], rsp
    mov rax, offset stack_top64
    mov rsp, rax
    mov rax, qword ptr [rip + syscall_user_rax]

    # IRETQ frame for user return
    push 0x1B
    push qword ptr [rip + syscall_user_rsp]
    push r11
    push 0x23
    push rcx
    push 0
    push 128

    # Register save (match isr_common_stub order)
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

    add rsp, 16
    iretq

    .section .bss
    .align 8
syscall_user_rsp:
    .quad 0
syscall_user_rax:
    .quad 0

    .section .note.GNU-stack,"",@progbits
