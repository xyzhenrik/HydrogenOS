#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Checked access to user memory from syscall context.
 *
 * Every helper walks the active page tables and only accepts pages that user
 * mode itself could access: canonical lower-half addresses that are mapped
 * with the user bit at every paging level (and writable for USER_ACCESS_WRITE).
 * Supervisor-only memory, such as the identity-mapped kernel, is rejected even
 * though its address lies below the user/kernel split.
 *
 * The validation and the access that follows run with interrupts disabled on
 * a single CPU, and a process cannot remap its own memory while it is inside
 * a syscall, so a validated range stays valid until the syscall returns or the
 * process is resumed from a blocking wait.
 */

/* First address that is not user memory; everything below is the lower half. */
#define USER_ADDRESS_LIMIT 0x0000800000000000ULL

typedef enum UserAccess {
    USER_ACCESS_READ,
    USER_ACCESS_WRITE,
} UserAccess;

/*
 * Returns 0 if every byte of [uptr, uptr + len) is accessible, else -EFAULT.
 * Rejects ranges that wrap around or leave the user half. A zero-length range
 * is always valid. Use this to validate large buffers that are then accessed
 * in place (framebuffer blits, read/write buffers).
 */
int user_range_validate(const void *uptr, size_t len, UserAccess access);

/* Copy len bytes in/out of user space; 0 or -EFAULT, nothing is copied on error. */
int copy_from_user(void *kdst, const void *usrc, size_t len);
int copy_to_user(void *udst, const void *ksrc, size_t len);

/*
 * Copies the NUL-terminated user string at usrc into kdst (dst_size bytes
 * including the terminator). Returns the string length without the NUL,
 * -EFAULT if a page of the string is not readable, or -ERANGE if it does not
 * fit; in the latter case kdst is still NUL-terminated.
 */
long strncpy_from_user(char *kdst, const char *usrc, size_t dst_size);
