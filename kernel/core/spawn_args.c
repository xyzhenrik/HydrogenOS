#include "spawn_args.h"

#include "paging.h"

#include <stddef.h>

static uint32_t spawn_args_next(const spawn_args_t *args, uint32_t off) {
    while (off < args->bytes && args->strings[off] != 0)
        off++;
    return off + 1;
}

int spawn_args_place(uint64_t *pml4, regs_t *frame, const spawn_args_t *args) {
    if (!pml4 || !frame || !args)
        return -1;
    if (args->argc + args->envc > SPAWN_ARGS_MAX_ENTRIES ||
        args->bytes > SPAWN_ARGS_MAX_BYTES)
        return -1;

    uint32_t slots = args->argc + 1 + args->envc + 1;
    uintptr_t strings_va = (frame->rsp - args->bytes) & ~(uintptr_t)15;
    uintptr_t vector_va =
        (strings_va - (uintptr_t)slots * sizeof(uint64_t)) & ~(uintptr_t)15;

    uint64_t *prev_pml4 = paging_current_pml4();
    paging_switch(pml4);
    char *strings = (char *)strings_va;
    uint64_t *vector = (uint64_t *)vector_va;
    for (uint32_t i = 0; i < args->bytes; ++i)
        strings[i] = args->strings[i];

    uint32_t off = 0;
    uint32_t slot = 0;
    for (uint32_t i = 0; i < args->argc; ++i) {
        vector[slot++] = (uint64_t)(strings_va + off);
        off = spawn_args_next(args, off);
    }
    vector[slot++] = 0;
    for (uint32_t i = 0; i < args->envc; ++i) {
        vector[slot++] = (uint64_t)(strings_va + off);
        off = spawn_args_next(args, off);
    }
    vector[slot] = 0;
    paging_switch(prev_pml4);

    frame->rsp = vector_va;
    frame->rdi = args->argc;
    frame->rsi = vector_va;
    frame->rdx = vector_va + (uint64_t)(args->argc + 1) * sizeof(uint64_t);
    return 0;
}

int spawn_args_apply(process_t *proc, const spawn_args_t *args) {
    if (!proc || !proc->has_frame)
        return -1;
    return spawn_args_place(proc->pml4, &proc->frame, args);
}
