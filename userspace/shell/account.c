#include "account.h"

#include "errno.h"
#include "mini_libc.h"
#include "output.h"
#include "passwd.h"
#include "syscall.h"

enum {
    STATUS_FAILURE = 1,
    STATUS_USAGE = 2,
    MODE_STRING_LEN = 10,
    OWNER_SPEC_CAP = 2 * PASSWD_NAME_MAX + 2,
};

static const char chmod_usage[] = "usage: chmod <mode> <file>...  (octal or [ugoa][+-=][rwx])";
static const char chown_usage[] = "usage: chown <owner>[:<group>] <file>...";

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int all_digits(const char *s, size_t len) {
    if (len == 0)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (!is_digit(s[i]))
            return 0;
    }
    return 1;
}

static const char *failure_reason(long rc) {
    switch (-rc) {
    case EPERM:
        return "operation not permitted";
    case EACCES:
        return "permission denied";
    case ENOENT:
        return "no such file or directory";
    case EINVAL:
        return "invalid argument";
    default:
        return "cannot change";
    }
}

void account_format_mode(uint32_t mode, char out[11]) {
    static const char rwx[] = "rwx";
    out[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : '-';
    for (int i = 0; i < MODE_STRING_LEN - 1; ++i)
        out[i + 1] = (mode & (0400u >> i)) ? rwx[i % 3] : '-';
    out[MODE_STRING_LEN] = '\0';
}

void account_format_owner(uint32_t id, char *out, size_t cap) {
    passwd_entry_t entry;
    if (passwd_find_uid(id, &entry) == 0)
        snprintf(out, cap, "%s", entry.name);
    else
        snprintf(out, cap, "%u", id);
}

/* Applies the symbolic mode clauses in spec ("u+x,go-w") to current. */
static int apply_symbolic_mode(const char *spec, uint32_t current, uint32_t *out) {
    uint32_t mode = current;
    const char *p = spec;
    for (;;) {
        uint32_t who = 0;
        uint32_t bits = 0;
        for (;; ++p) {
            if (*p == 'u')
                who |= 0700;
            else if (*p == 'g')
                who |= 0070;
            else if (*p == 'o')
                who |= 0007;
            else if (*p == 'a')
                who |= S_IRWXUGO;
            else
                break;
        }
        if (who == 0)
            who = S_IRWXUGO;
        char op = *p++;
        if (op != '+' && op != '-' && op != '=')
            return -1;
        for (;; ++p) {
            if (*p == 'r')
                bits |= 4;
            else if (*p == 'w')
                bits |= 2;
            else if (*p == 'x')
                bits |= 1;
            else
                break;
        }
        uint32_t mask = bits * 0111 & who;
        if (op == '+')
            mode |= mask;
        else if (op == '-')
            mode &= ~mask;
        else
            mode = (mode & ~who) | mask;
        if (*p == '\0')
            break;
        if (*p++ != ',')
            return -1;
    }
    *out = mode;
    return 0;
}

static int apply_mode(const char *spec, uint32_t current, uint32_t *out) {
    size_t len = strlen(spec);
    if (!all_digits(spec, len))
        return apply_symbolic_mode(spec, current, out);
    uint32_t mode = 0;
    for (size_t i = 0; i < len; ++i) {
        if (spec[i] > '7' || mode > S_IRWXUGO)
            return -1;
        mode = mode << 3 | (uint32_t)(spec[i] - '0');
    }
    if (mode > S_IRWXUGO)
        return -1;
    *out = mode;
    return 0;
}

static int builtin_chmod(int argc, char **argv) {
    int status = 0;
    if (argc < 3) {
        out_error("chmod", 0, chmod_usage);
        return STATUS_USAGE;
    }
    for (int i = 2; i < argc; ++i) {
        stat_t st;
        uint32_t mode = 0;
        long rc = stat(argv[i], &st);
        if (rc == 0 && apply_mode(argv[1], st.st_mode & S_IRWXUGO, &mode) != 0) {
            out_error("chmod", argv[1], "invalid mode");
            return STATUS_USAGE;
        }
        if (rc == 0)
            rc = sys_chmod(argv[i], mode);
        if (rc != 0) {
            out_error("chmod", argv[i], failure_reason(rc));
            status = STATUS_FAILURE;
        }
    }
    return status;
}

/* An account name or a number; the empty string leaves the id unchanged.
 * Groups are named after the account whose primary group they are. */
static int parse_owner_id(const char *text, size_t len, int group, uint32_t *out) {
    char name[PASSWD_NAME_MAX];
    passwd_entry_t entry;
    if (len == 0) {
        *out = ID_UNCHANGED;
        return 0;
    }
    if (len >= sizeof(name))
        return -1;
    memcpy(name, text, len);
    name[len] = '\0';
    if (all_digits(name, len)) {
        unsigned long value = strtoul(name, 0, 10);
        if (value >= ID_UNCHANGED)
            return -1;
        *out = (uint32_t)value;
        return 0;
    }
    if (passwd_find_name(name, &entry) != 0)
        return -1;
    *out = group ? entry.gid : entry.uid;
    return 0;
}

static int builtin_chown(int argc, char **argv) {
    int status = 0;
    uint32_t uid;
    uint32_t gid;
    const char *colon;
    size_t owner_len;
    if (argc < 3) {
        out_error("chown", 0, chown_usage);
        return STATUS_USAGE;
    }
    colon = strchr(argv[1], ':');
    owner_len = colon ? (size_t)(colon - argv[1]) : strlen(argv[1]);
    if (parse_owner_id(argv[1], owner_len, 0, &uid) != 0 ||
        parse_owner_id(colon ? colon + 1 : "", colon ? strlen(colon + 1) : 0, 1, &gid) != 0 ||
        (uid == ID_UNCHANGED && gid == ID_UNCHANGED)) {
        out_error("chown", argv[1], "invalid owner");
        return STATUS_USAGE;
    }
    for (int i = 2; i < argc; ++i) {
        long rc = sys_chown(argv[i], uid, gid);
        if (rc != 0) {
            out_error("chown", argv[i], failure_reason(rc));
            status = STATUS_FAILURE;
        }
    }
    return status;
}

static int builtin_id(int argc, char **argv) {
    char user[OWNER_SPEC_CAP];
    char group[OWNER_SPEC_CAP];
    char line[2 * OWNER_SPEC_CAP + 32];
    uint32_t uid = (uint32_t)sys_getuid();
    uint32_t gid = (uint32_t)sys_getgid();
    (void)argv;
    if (argc > 1) {
        out_error("id", 0, "usage: id");
        return STATUS_USAGE;
    }
    account_format_owner(uid, user, sizeof(user));
    account_format_owner(gid, group, sizeof(group));
    snprintf(line, sizeof(line), "uid=%u(%s) gid=%u(%s)\n", uid, user, gid, group);
    out_str(1, line);
    return 0;
}

static int builtin_whoami(int argc, char **argv) {
    char user[OWNER_SPEC_CAP];
    (void)argv;
    if (argc > 1) {
        out_error("whoami", 0, "usage: whoami");
        return STATUS_USAGE;
    }
    account_format_owner((uint32_t)sys_getuid(), user, sizeof(user));
    out_str(1, user);
    out_char(1, '\n');
    return 0;
}

typedef struct AccountEntry {
    const char *name;
    BuiltinFn fn;
} AccountEntry;

static const AccountEntry commands[] = {
    {"chmod", builtin_chmod},
    {"chown", builtin_chown},
    {"id", builtin_id},
    {"whoami", builtin_whoami},
};

BuiltinFn account_find(const char *name) {
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(commands[i].name, name) == 0)
            return commands[i].fn;
    }
    return 0;
}
