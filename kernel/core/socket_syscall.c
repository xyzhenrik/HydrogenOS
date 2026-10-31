#include "socket_syscall.h"

#include "errno.h"
#include "net/socket.h"
#include "syscall.h"
#include "usercopy.h"

#define SOCKET_WOULD_BLOCK 1

/* 0 and the socket behind fd, or -EBADF / -ENOTSOCK. */
static int socket_lookup(process_t *proc, int fd, net_socket_t **out) {
    int rc = process_fd_socket(proc, fd, out);

    if (rc == -1)
        return -EBADF;
    if (rc == -2)
        return -ENOTSOCK;
    return 0;
}

/* Runs one blocking operation with the arguments in 'regs'. Returns 0 with
 * *result set, or SOCKET_WOULD_BLOCK. 'resuming' marks a repeat after a wait:
 * connect then only checks how the handshake ended. The user buffers are
 * validated on every attempt because the caller may have changed its mappings
 * while blocked. */
static int socket_execute(process_t *proc, int fd, const regs_t *regs,
                          net_wait_kind_t kind, int resuming, uint64_t *result) {
    net_socket_t *socket = 0;
    int rc = socket_lookup(proc, fd, &socket);

    if (rc != 0) {
        *result = (uint64_t)(int64_t)rc;
        return 0;
    }

    switch (kind) {
    case NET_WAIT_READ: {
        uint8_t *buf = (uint8_t *)(uintptr_t)regs->rsi;
        size_t len = (size_t)regs->rdx;
        uint32_t got = 0;

        if (user_range_validate(buf, len, USER_ACCESS_WRITE) != 0) {
            *result = (uint64_t)-EFAULT;
            return 0;
        }
        rc = net_socket_stream_read(socket, buf, len, &got);
        if (rc == -EAGAIN)
            return SOCKET_WOULD_BLOCK;
        *result = rc == 0 ? got : (uint64_t)(int64_t)rc;
        return 0;
    }
    case NET_WAIT_WRITE: {
        const uint8_t *buf = (const uint8_t *)(uintptr_t)regs->rsi;
        size_t len = (size_t)regs->rdx;
        uint32_t sent = 0;

        if (user_range_validate(buf, len, USER_ACCESS_READ) != 0) {
            *result = (uint64_t)-EFAULT;
            return 0;
        }
        rc = net_socket_stream_write(socket, buf, len, &sent);
        if (rc == -EAGAIN)
            return SOCKET_WOULD_BLOCK;
        *result = rc == 0 ? sent : (uint64_t)(int64_t)rc;
        return 0;
    }
    case NET_WAIT_ACCEPT: {
        net_sockaddr_t *peer = (net_sockaddr_t *)(uintptr_t)regs->rsi;
        net_socket_t *child = 0;
        int child_fd;

        if (peer && user_range_validate(peer, sizeof(*peer), USER_ACCESS_WRITE) != 0) {
            *result = (uint64_t)-EFAULT;
            return 0;
        }
        rc = net_socket_accept_ready(socket);
        if (rc == -EAGAIN)
            return SOCKET_WOULD_BLOCK;
        if (rc == 0)
            rc = net_socket_create(NET_AF_INET, NET_SOCK_STREAM, 0, &child);
        if (rc != 0) {
            *result = (uint64_t)(int64_t)rc;
            return 0;
        }
        child_fd = process_handle_attach_socket(proc, child);
        if (child_fd < 0) {
            net_socket_release(child);
            *result = (uint64_t)-EMFILE;
            return 0;
        }
        /* Everything that can fail is behind us: the queued connection only
         * leaves the accept queue once its socket and handle exist. */
        net_socket_accept(socket, child, peer);
        *result = (uint64_t)child_fd;
        return 0;
    }
    case NET_WAIT_CONNECT:
        if (!resuming) {
            if (regs->rdx > NET_PORT_MAX) {
                *result = (uint64_t)-EINVAL;
                return 0;
            }
            rc = net_socket_connect(socket, (uint32_t)regs->rsi, (uint16_t)regs->rdx);
            if (rc != 0) {
                *result = (uint64_t)(int64_t)rc;
                return 0;
            }
        }
        rc = net_socket_connect_status(socket);
        if (rc == -EAGAIN)
            return SOCKET_WOULD_BLOCK;
        *result = (uint64_t)(int64_t)rc;
        return 0;
    default:
        *result = (uint64_t)-EINVAL;
        return 0;
    }
}

/* Runs the call and, when it cannot finish, parks the process on its socket. */
static uint64_t socket_run(process_t *proc, const regs_t *r, net_wait_kind_t kind) {
    uint64_t result = 0;

    if (socket_execute(proc, (int)r->rdi, r, kind, 0, &result) == 0)
        return result;
    if (process_begin_socket_wait(proc, (int)r->rdi, kind) != 0)
        return (uint64_t)-EBADF;
    return 0;
}

uint64_t socket_syscall_dispatch(process_t *proc, const regs_t *r) {
    net_socket_t *socket = 0;
    int rc;

    switch (r->rax) {
    case SYS_CONNECT:
        return socket_run(proc, r, NET_WAIT_CONNECT);
    case SYS_ACCEPT:
        return socket_run(proc, r, NET_WAIT_ACCEPT);
    case SYS_LISTEN:
        rc = socket_lookup(proc, (int)r->rdi, &socket);
        if (rc == 0)
            rc = net_socket_listen(socket, (int)r->rsi);
        return (uint64_t)(int64_t)rc;
    case SYS_SHUTDOWN_SOCK:
        rc = socket_lookup(proc, (int)r->rdi, &socket);
        if (rc == 0)
            rc = net_socket_shutdown(socket, (int)r->rsi);
        return (uint64_t)(int64_t)rc;
    default:
        return (uint64_t)-ENOSYS;
    }
}

int socket_syscall_stream_io(process_t *proc, const regs_t *r, int is_write,
                             uint64_t *result) {
    net_socket_t *socket = 0;

    if (process_fd_socket(proc, (int)r->rdi, &socket) != 0 ||
        !net_socket_is_stream(socket))
        return 0;
    *result = socket_run(proc, r, is_write ? NET_WAIT_WRITE : NET_WAIT_READ);
    return 1;
}

int socket_syscall_resume(process_t *p) {
    uint64_t result = 0;

    if (socket_execute(p, p->waiting_socket_fd, &p->frame,
                       (net_wait_kind_t)p->waiting_socket_kind, 1, &result) != 0) {
        p->state = PROCESS_BLOCKED_SOCKET;
        return -1;
    }
    p->waiting_socket_fd = -1;
    p->frame.rax = result;
    return 0;
}
