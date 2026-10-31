#include "exit_record.h"

#include "kheap.h"

struct exit_record {
    exit_record_t *next;
    uint32_t pid;
    int status;
};

exit_record_t *exit_record_create(void) {
    return (exit_record_t *)kalloc(sizeof(exit_record_t));
}

void exit_record_destroy(exit_record_t *record) {
    kfree(record);
}

void exit_record_list_destroy(exit_record_t **head) {
    exit_record_t *record = *head;
    *head = 0;
    while (record) {
        exit_record_t *next = record->next;
        kfree(record);
        record = next;
    }
}

void exit_record_publish(exit_record_t **head, exit_record_t *record,
                         uint32_t pid, int status) {
    record->pid = pid;
    record->status = status;
    record->next = *head;
    *head = record;
}

int exit_record_claim(exit_record_t **head, uint32_t pid, int *out_status) {
    for (exit_record_t **link = head; *link; link = &(*link)->next) {
        exit_record_t *record = *link;
        if (record->pid != pid)
            continue;
        if (out_status)
            *out_status = record->status;
        *link = record->next;
        kfree(record);
        return 1;
    }
    return 0;
}
