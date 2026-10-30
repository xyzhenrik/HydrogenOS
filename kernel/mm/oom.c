#include "oom.h"

#include "core/process.h"
#include "core/signal.h"
#include "kheap.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "syscall.h"

/*
 * Victim choice. A candidate's score is its resident user pages weighted by
 * its priority, so that of two equally large processes the less important one
 * dies first. Three passes widen the pool only when the narrower one is
 * empty:
 *   1. neither init nor the caller's session leader, no HIGH or RT process;
 *   2. neither init nor the caller's session leader;
 *   3. anything but init.
 * The session leader is the caller's ancestor directly below init (or the
 * caller itself): killing it takes the whole session down.
 */
enum {
    OOM_PASS_COUNT = 3,
    OOM_PASS_NO_SYSTEM_SERVICES = 0,
    OOM_PASS_ANY_NON_LEADER = 1,
    OOM_ANCESTOR_DEPTH_LIMIT = 64,
    OOM_PRIORITY_LEVELS = PROCESS_PRIO_RT + 1,
};

static uint64_t kills;
static int reclaiming;

static const process_t *session_leader(const process_t *caller) {
    if (!caller || caller == process_kernel())
        return 0;
    const process_t *leader = caller;
    for (int depth = 0; depth < OOM_ANCESTOR_DEPTH_LIMIT; ++depth) {
        const process_t *parent = process_find_by_pid(leader->parent_pid);
        if (!parent || syscall_is_bootstrap(parent))
            break;
        leader = parent;
    }
    return leader;
}

static uint64_t oom_score(size_t pages, uint8_t priority) {
    uint8_t clamped = priority < OOM_PRIORITY_LEVELS ? priority : PROCESS_PRIO_RT;
    return (uint64_t)pages * (uint64_t)(OOM_PRIORITY_LEVELS - clamped);
}

static process_t *pick_victim(const process_t *leader) {
    for (int pass = 0; pass < OOM_PASS_COUNT; ++pass) {
        process_t *best = 0;
        uint64_t best_score = 0;
        size_t best_pages = 0;
        for (process_t *p = process_table_next(0); p; p = process_table_next(p)) {
            if (!p->alive || p == process_kernel() || syscall_is_bootstrap(p) ||
                p->pml4 == paging_kernel_pml4())
                continue;
            if (pass <= OOM_PASS_ANY_NON_LEADER && p == leader)
                continue;
            if (pass == OOM_PASS_NO_SYSTEM_SERVICES && p->priority >= PROCESS_PRIO_HIGH)
                continue;
            size_t pages = paging_count_user_pages(p->pml4);
            uint64_t score = oom_score(pages, p->priority);
            if (pages != 0 && score > best_score) {
                best = p;
                best_score = score;
                best_pages = pages;
            }
        }
        if (best) {
            serial_write("oom: killing pid ");
            serial_write_decimal(best->pid);
            serial_write(" (");
            serial_write(best->name);
            serial_write(") rss=");
            serial_write_decimal(best_pages);
            serial_write(" pages score=");
            serial_write_decimal(best_score);
            serial_write("\n");
            return best;
        }
    }
    return 0;
}

/* Returns 1 if a victim's memory was released, 0 if nothing was or can be
 * released right now. */
static int oom_reclaim(void) {
    /* The kill path frees memory only; the guard is for a future change that
       would allocate from it. */
    if (reclaiming)
        return 0;
    reclaiming = 1;

    process_t *caller = process_current();
    process_t *victim = pick_victim(session_leader(caller));
    int released = 0;
    if (victim) {
        kills++;
        signal_terminate(victim, SIGKILL);
        /* The caller is running on its own address space and kernel state, so
           it can only be reaped by the scheduler once it has returned. */
        if (victim != caller) {
            process_destroy(victim);
            released = 1;
        }
    }
    reclaiming = 0;
    return released;
}

uintptr_t oom_alloc_page(void) {
    uintptr_t page = pmm_alloc_page();
    if (page)
        return page;
    if (!oom_reclaim())
        return 0;
    return pmm_alloc_page();
}

void *oom_kalloc(size_t size) {
    void *block = kalloc(size);
    if (block)
        return block;
    if (!oom_reclaim())
        return 0;
    return kalloc(size);
}

uint64_t oom_kill_count(void) {
    return kills;
}
