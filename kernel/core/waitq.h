#pragma once

#include <stdint.h>

/*
 * Wait queues: intrusive FIFO lists of waiters that an event source wakes
 * without knowing who they are. An entry is embedded in the waiting object,
 * so queueing never allocates and a waiter can sit on several queues at once
 * (one entry per queue).
 *
 * Every operation is IRQ-safe: interrupts are masked for the duration and
 * their previous state is restored, so queues may be used from interrupt
 * handlers and nested critical sections. The kernel runs on one CPU; masking
 * interrupts is the whole locking story.
 *
 * Lifetime rule: an entry must be removed (wait_queue_remove or
 * wait_queue_cancel) before the memory holding it, or the queue holding it,
 * is freed or reused.
 */

typedef struct WaitEntry WaitEntry;
typedef struct WaitQueue WaitQueue;

/* Called with the entry already unlinked from its queue. 'key' is whatever the
 * waker passed to wait_queue_wake_one()/wait_queue_wake_all(); it is null when
 * a cancelled exclusive waiter hands its wake on. Runs with interrupts masked
 * and must not block. It may add or remove entries of other queues, but must
 * not add the entry being woken back to the queue it was woken from. */
typedef void (*WaitWakeFn)(WaitEntry *entry, void *key);

struct WaitQueue {
    WaitEntry *head;
    WaitEntry *tail;
};

struct WaitEntry {
    WaitEntry *next;
    WaitEntry *prev;
    /* Queue the entry is linked in; after a wake, the queue it was woken
     * from. Null while the entry is idle. */
    WaitQueue *queue;
    WaitWakeFn wake;
    void *owner;
    uint8_t linked;
    uint8_t woken;
    /* A woken exclusive waiter consumed a wait_queue_wake_one() all by
     * itself: if it leaves without acting on it, wait_queue_cancel() passes
     * the wake on. */
    uint8_t exclusive;
};

void wait_queue_init(WaitQueue *queue);
int wait_queue_empty(const WaitQueue *queue);

/* Prepares an idle entry. 'wake' is invoked whenever the entry is woken. */
void wait_entry_init(WaitEntry *entry, WaitWakeFn wake, void *owner,
                     int exclusive);

/* Appends the entry at the tail. An entry that is still linked is moved. */
void wait_queue_add(WaitQueue *queue, WaitEntry *entry);

/* Unlinks the entry if it is linked and returns it to the idle state, whether
 * or not it was woken. Safe on an idle entry. */
void wait_queue_remove(WaitEntry *entry);

/* Like wait_queue_remove(), but an exclusive entry that was woken without
 * having acted on it hands the wake to the next waiter of its queue. Used
 * when a waiter gives up (interrupted, killed). */
void wait_queue_cancel(WaitEntry *entry);

/* Unlink and wake the oldest waiter, or every waiter in queue order. They
 * return the number of entries woken. */
uint32_t wait_queue_wake_one(WaitQueue *queue, void *key);
uint32_t wait_queue_wake_all(WaitQueue *queue, void *key);
