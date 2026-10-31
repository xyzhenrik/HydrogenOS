#pragma once

#include <stdint.h>

#define PASSWD_FILE "/etc/passwd"
#define PASSWD_NAME_MAX 32
#define PASSWD_PATH_MAX 64

typedef struct passwd_entry {
    char name[PASSWD_NAME_MAX];
    uint32_t uid;
    uint32_t gid;
    char home[PASSWD_PATH_MAX];
    char shell[PASSWD_PATH_MAX];
} passwd_entry_t;

/*
 * Looks an account up in /etc/passwd, whose lines read
 * name:password:uid:gid:comment:home:shell. Malformed lines, overlong lines
 * and fields that do not fit are skipped. Both calls return 0 and fill *out,
 * or -1 when no account matches or the file cannot be read.
 */
int passwd_find_name(const char *name, passwd_entry_t *out);
int passwd_find_uid(uint32_t uid, passwd_entry_t *out);
