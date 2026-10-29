#pragma once

#include <stdint.h>

/*
 * The identity a process acts under. euid decides every permission check;
 * uid is what sys_getuid reports and bounds what an unprivileged process may
 * switch to. A process inherits all three from its creator.
 */
typedef struct credentials {
    uint32_t uid;
    uint32_t euid;
    uint32_t gid;
} credentials_t;

#define CREDENTIALS_ROOT_UID 0u
/* Never a valid id: stands for "leave unchanged" where an id is optional. */
#define CREDENTIALS_ID_NONE 0xFFFFFFFFu

static inline int credentials_is_root(const credentials_t *cred) {
    return cred->euid == CREDENTIALS_ROOT_UID;
}

/* Root may become any user; everyone else only the user they already are.
   Both return 0, -EINVAL for CREDENTIALS_ID_NONE or -EPERM. */
int credentials_set_uid(credentials_t *cred, uint32_t uid);
int credentials_set_gid(credentials_t *cred, uint32_t gid);
