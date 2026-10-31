#include "credentials.h"

#include "errno.h"

int credentials_set_uid(credentials_t *cred, uint32_t uid) {
    if (uid == CREDENTIALS_ID_NONE)
        return -EINVAL;
    if (!credentials_is_root(cred) && uid != cred->uid)
        return -EPERM;
    cred->uid = uid;
    cred->euid = uid;
    return 0;
}

int credentials_set_gid(credentials_t *cred, uint32_t gid) {
    if (gid == CREDENTIALS_ID_NONE)
        return -EINVAL;
    if (!credentials_is_root(cred) && gid != cred->gid)
        return -EPERM;
    cred->gid = gid;
    return 0;
}
