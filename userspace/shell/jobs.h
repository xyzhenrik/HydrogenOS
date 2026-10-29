#pragma once

#include <stddef.h>

enum {
    JOBS_MAX = 16,
    JOB_TEXT_MAX = 96,
};

/* Registers a background pipeline. The job's status is the status of the
 * last pid. Returns the job id (>= 1), or -1 when the table is full. */
int jobs_add(const int *pids, int count, const char *text, size_t text_len);

/* Reaps finished job processes without blocking. */
void jobs_update(void);

/* Prints jobs to stdout; finished jobs are printed once and then dropped.
 * With only_finished set, running jobs are skipped. */
void jobs_print(int only_finished);

/* Blocks until job `id` finishes, or every job when id is 0, and drops the
 * waited jobs. Returns 0 with the job's status in *status (always 0 when
 * waiting for every job), or -1 when no such job exists. */
int jobs_wait(int id, int *status);

/* Waits for one child pid, keeping the job table consistent when the pid
 * belongs to a job. Returns 0, or -1 when pid is not a child. */
int jobs_wait_pid(int pid, int *status);

/* Sends sig to every unfinished process of job `id`. Returns 0, or -1 when no
 * such job exists. */
int jobs_signal(int id, int sig);

/* Shell exit code for a wait status: 128 + signal number for a process that
 * was killed by a signal, the status itself otherwise. */
int jobs_exit_code(int wait_status);
