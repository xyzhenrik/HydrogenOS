#include "spawn_args.h"

#include "paging.h"

#include <stddef.h>

static uint32_t spawn_args_next(const spawn_args_t *args, uint32_t off) {
    while (off < args->bytes && args->strings[off] != 0)
        off++;
    return off + 1;
}

int spawn_args_apply(process_t *proc, const spawn_args_t *args) {
    if (!proc || !args || !proc->pml4 || !proc->has_frame)
        return -1;
    if (args->argc + args->envc > SPAWN_ARGS_MAX_ENTRIES ||
        args->bytes > SPAWN_ARGS_MAX_BYTES)
        return -1;

    uint32_t slots = args->argc + 1 + args->envc + 1;
    uintptr_t strings_va = (proc->frame.rsp - args->bytes) & ~(uintptr_t)15;
    uintptr_t vector_va =
        (strings_va - (uintptr_t)slots * sizeof(uint64_t)) & ~(uintptr_t)15;

    uint64_t *prev_pml4 = paging_current_pml4();
    paging_switch(proc->pml4);
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

    proc->frame.rsp = vector_va;
    proc->frame.rdi = args->argc;
    proc->frame.rsi = vector_va;
    proc->frame.rdx = vector_va + (uint64_t)(args->argc + 1) * sizeof(uint64_t);
    return 0;
}
