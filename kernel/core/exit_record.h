#pragma once

#include <stdint.h>

/*
 * Exit status of a terminated child that its parent has not waited for yet.
 *
 * A record is created together with the child, so that terminating a process
 * can never fail for lack of memory: at exit the child's record is linked
 * into its parent's list, and a successful wait unlinks and frees it. A list
 * owned by a process is destroyed with that process. There is no fixed
 * capacity and nothing is overwritten.
 */
typedef struct exit_record exit_record_t;

/* Returns 0 if out of memory. */
exit_record_t *exit_record_create(void);

/* Frees a record that is not linked into a list. */
void exit_record_destroy(exit_record_t *record);

/* Frees every record of the list and leaves it empty. */
void exit_record_list_destroy(exit_record_t **head);

/* Fills in 'record' and links it into the list. */
void exit_record_publish(exit_record_t **head, exit_record_t *record,
                         uint32_t pid, int status);

/* Unlinks and frees the record of 'pid'. Returns 1 and stores its status in
 * 'out_status' (optional) if there was one, else 0. */
int exit_record_claim(exit_record_t **head, uint32_t pid, int *out_status);
