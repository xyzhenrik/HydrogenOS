#include "jobs.h"

#include "mini_libc.h"
#include "output.h"
#include "shell_limits.h"

/* Status reported for a job process whose exit status could not be
 * collected (it was not, or no longer, a child of this shell). */
#define JOB_STATUS_LOST 127

typedef struct ShellJob {
    int used;
    int pids[SHELL_MAX_STAGES];
    int finished[SHELL_MAX_STAGES];
    int statuses[SHELL_MAX_STAGES];
    int count;
    char text[JOB_TEXT_MAX];
} ShellJob;

static ShellJob jobs[JOBS_MAX];

static int job_done(const ShellJob *job) {
    for (int i = 0; i < job->count; ++i) {
        if (!job->finished[i])
            return 0;
    }
    return 1;
}

static int job_status(const ShellJob *job) {
    return job->count > 0 ? job->statuses[job->count - 1] : 0;
}

int jobs_add(const int *pids, int count, const char *text, size_t text_len) {
    if (count <= 0 || count > SHELL_MAX_STAGES)
        return -1;
    for (int slot = 0; slot < JOBS_MAX; ++slot) {
        ShellJob *job = &jobs[slot];
        if (job->used)
            continue;
        job->used = 1;
        job->count = count;
        for (int i = 0; i < count; ++i) {
            job->pids[i] = pids[i];
            job->finished[i] = 0;
            job->statuses[i] = 0;
        }
        if (text_len >= JOB_TEXT_MAX)
            text_len = JOB_TEXT_MAX - 1;
        memcpy(job->text, text, text_len);
        job->text[text_len] = 0;
        return slot + 1;
    }
    return -1;
}

static void reap(ShellJob *job, int index, int blocking) {
    int status = 0;
    long rc;
    if (job->finished[index])
        return;
    rc = blocking ? wait_pid(job->pids[index], &status)
                  : try_wait_pid(job->pids[index], &status);
    if (rc == 0)
        return;
    job->finished[index] = 1;
    job->statuses[index] = rc > 0 ? status : JOB_STATUS_LOST;
}

void jobs_update(void) {
    for (int slot = 0; slot < JOBS_MAX; ++slot) {
        if (!jobs[slot].used)
            continue;
        for (int i = 0; i < jobs[slot].count; ++i)
            reap(&jobs[slot], i, 0);
    }
}

void jobs_print(int only_finished) {
    jobs_update();
    for (int slot = 0; slot < JOBS_MAX; ++slot) {
        ShellJob *job = &jobs[slot];
        int done;
        if (!job->used)
            continue;
        done = job_done(job);
        if (only_finished && !done)
            continue;
        out_char(1, '[');
        out_u64(1, (unsigned long)(slot + 1));
        out_str(1, "] ");
        if (!done) {
            out_str(1, "Running  ");
        } else if (job_status(job) == 0) {
            out_str(1, "Done     ");
        } else {
            out_str(1, "Exit ");
            out_u64(1, (unsigned long)(unsigned int)job_status(job));
            out_str(1, "   ");
        }
        out_str(1, job->text);
        out_char(1, '\n');
        if (done)
            job->used = 0;
    }
    out_flush();
}

int jobs_wait(int id, int *status) {
    int found = 0;
    for (int slot = 0; slot < JOBS_MAX; ++slot) {
        ShellJob *job = &jobs[slot];
        if (!job->used || (id != 0 && slot + 1 != id))
            continue;
        for (int i = 0; i < job->count; ++i)
            reap(job, i, 1);
        *status = job_status(job);
        job->used = 0;
        found = 1;
    }
    if (id == 0) {
        *status = 0;
        return 0;
    }
    return found ? 0 : -1;
}

int jobs_wait_pid(int pid, int *status) {
    for (int slot = 0; slot < JOBS_MAX; ++slot) {
        ShellJob *job = &jobs[slot];
        if (!job->used)
            continue;
        for (int i = 0; i < job->count; ++i) {
            if (job->pids[i] != pid)
                continue;
            reap(job, i, 1);
            *status = job->statuses[i];
            if (job_done(job))
                job->used = 0;
            return 0;
        }
    }
    if (wait_pid(pid, status) < 0)
        return -1;
    return 0;
}
