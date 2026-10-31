#include "waitq.h"

#define WAITQ_RFLAGS_IF 0x200

static uint64_t waitq_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void waitq_irq_restore(uint64_t flags) {
    if (flags & WAITQ_RFLAGS_IF)
        __asm__ volatile("sti" : : : "memory");
}

static void waitq_unlink(WaitEntry *entry) {
    WaitQueue *queue = entry->queue;

    if (entry->prev)
        entry->prev->next = entry->next;
    else
        queue->head = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    else
        queue->tail = entry->prev;
    entry->next = 0;
    entry->prev = 0;
    entry->linked = 0;
}

/* Pops the head and runs its wake function. Returns 0 on an empty queue. */
static int waitq_wake_head(WaitQueue *queue, void *key) {
    WaitEntry *entry = queue->head;

    if (!entry)
        return 0;
    waitq_unlink(entry);
    entry->woken = 1;
    entry->wake(entry, key);
    return 1;
}

void wait_queue_init(WaitQueue *queue) {
    queue->head = 0;
    queue->tail = 0;
}

int wait_queue_empty(const WaitQueue *queue) {
    return queue->head == 0;
}

void wait_entry_init(WaitEntry *entry, WaitWakeFn wake, void *owner,
                     int exclusive) {
    entry->next = 0;
    entry->prev = 0;
    entry->queue = 0;
    entry->wake = wake;
    entry->owner = owner;
    entry->linked = 0;
    entry->woken = 0;
    entry->exclusive = exclusive != 0;
}

void wait_queue_add(WaitQueue *queue, WaitEntry *entry) {
    uint64_t flags = waitq_irq_save();

    if (entry->linked)
        waitq_unlink(entry);
    entry->queue = queue;
    entry->woken = 0;
    entry->prev = queue->tail;
    entry->next = 0;
    if (queue->tail)
        queue->tail->next = entry;
    else
        queue->head = entry;
    queue->tail = entry;
    entry->linked = 1;
    waitq_irq_restore(flags);
}

void wait_queue_remove(WaitEntry *entry) {
    uint64_t flags = waitq_irq_save();

    if (entry->linked)
        waitq_unlink(entry);
    entry->queue = 0;
    entry->woken = 0;
    waitq_irq_restore(flags);
}

void wait_queue_cancel(WaitEntry *entry) {
    uint64_t flags = waitq_irq_save();
    WaitQueue *handoff = (entry->woken && entry->exclusive) ? entry->queue : 0;

    if (entry->linked)
        waitq_unlink(entry);
    entry->queue = 0;
    entry->woken = 0;
    if (handoff)
        waitq_wake_head(handoff, 0);
    waitq_irq_restore(flags);
}

uint32_t wait_queue_wake_one(WaitQueue *queue, void *key) {
    uint64_t flags = waitq_irq_save();
    uint32_t woken = (uint32_t)waitq_wake_head(queue, key);

    waitq_irq_restore(flags);
    return woken;
}

uint32_t wait_queue_wake_all(WaitQueue *queue, void *key) {
    uint64_t flags = waitq_irq_save();
    uint32_t woken = 0;

    while (waitq_wake_head(queue, key))
        woken++;
    waitq_irq_restore(flags);
    return woken;
}
