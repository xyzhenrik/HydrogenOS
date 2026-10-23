.intel_syntax noprefix

.global task_switch_asm

# void task_switch_asm(uint32_t *old_esp, uint32_t *old_ebp, uint32_t *old_eip,
#                      uint32_t new_esp, uint32_t new_ebp, uint32_t new_eip)
# Saves current EIP/ESP/EBP into provided pointers, loads new, and jumps.

.globl task_switch_asm
task_switch_asm:
    # return address to caller is at [esp]
    mov eax, [esp]            # eax = return addr
    mov edx, [esp + 12]       # old_eip*
    mov [edx], eax

    mov eax, esp
    add eax, 4                # simulate "ret" (skip return addr)
    mov edx, [esp + 8]        # old_ebp*
    mov [edx], ebp
    mov edx, [esp + 4]        # old_esp*
    mov [edx], eax

    mov eax, [esp + 24]       # new_eip
    mov ecx, [esp + 20]       # new_ebp
    mov edx, [esp + 16]       # new_esp
    mov esp, edx              # switch stack
    mov ebp, ecx
    jmp eax                   # jump to new_eip

    .section .note.GNU-stack,"",@progbits
