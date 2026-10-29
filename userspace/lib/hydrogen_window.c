#include "hydrogen_window.h"
#include "syscall.h"

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#define HYDROGEN_WINDOW_BYTES_PER_PIXEL 4u

struct hydrogen_window {
    int width;
    int height;
    char *title;

    uint32_t *surface;
    size_t surface_bytes;

    int event_read_fd;
    int event_write_fd;

    unsigned char event_pending[sizeof(hydrogen_window_event_t)];
    size_t event_pending_len;

    int closed;
};

static size_t align_up_size(size_t value, size_t alignment) {
    size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

static void *alloc_pages(size_t bytes) {
    if (bytes == 0) {
        return 0;
    }
    size_t aligned = align_up_size(bytes, 4096u);
    void *ptr = (void *)sys_mmap(0, aligned, MMAP_PROT_READ | MMAP_PROT_WRITE, 0);
    if ((long)ptr < 0) {
        return 0;
    }
    return ptr;
}

static void free_pages(void *ptr, size_t bytes) {
    if (!ptr || bytes == 0) {
        return;
    }
    (void)sys_munmap(ptr, align_up_size(bytes, 4096u));
}

static void shift_pending(unsigned char *buf, size_t from, size_t to, size_t n) {
    if (from == to || n == 0) {
        return;
    }
    if (from < to) {
        for (size_t i = n; i > 0; --i) {
            buf[to + i - 1u] = buf[from + i - 1u];
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            buf[to + i] = buf[from + i];
        }
    }
}

hydrogen_window_t *hydrogen_window_create(int width, int height, const char *title) {
    if (width <= 0 || height <= 0) {
        return 0;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (pixel_count > (SIZE_MAX / HYDROGEN_WINDOW_BYTES_PER_PIXEL)) {
        return 0;
    }
    size_t surface_bytes = pixel_count * HYDROGEN_WINDOW_BYTES_PER_PIXEL;

    hydrogen_window_t *window = (hydrogen_window_t *)malloc(sizeof(*window));
    if (!window) {
        return 0;
    }
    memset(window, 0, sizeof(*window));

    window->width = width;
    window->height = height;
    window->event_read_fd = -1;
    window->event_write_fd = -1;

    if (title) {
        size_t title_len = strlen(title);
        window->title = (char *)malloc(title_len + 1u);
        if (!window->title) {
            free(window);
            return 0;
        }
        memcpy(window->title, title, title_len + 1u);
    } else {
        window->title = (char *)malloc(1u);
        if (window->title) {
            window->title[0] = '\0';
        }
    }

    window->surface = (uint32_t *)alloc_pages(surface_bytes);
    if (!window->surface) {
        free(window->title);
        free(window);
        return 0;
    }
    window->surface_bytes = surface_bytes;
    memset(window->surface, 0, surface_bytes);

    int fds[2] = { -1, -1 };
    if (sys_pipe(fds) != 0) {
        free_pages(window->surface, window->surface_bytes);
        free(window->title);
        free(window);
        return 0;
    }
    window->event_read_fd = fds[0];
    window->event_write_fd = fds[1];

    return window;
}

int hydrogen_window_destroy(hydrogen_window_t *window) {
    if (!window) {
        return 0;
    }

    if (window->event_read_fd >= 0) {
        (void)close(window->event_read_fd);
    }
    if (window->event_write_fd >= 0) {
        (void)close(window->event_write_fd);
    }

    free_pages(window->surface, window->surface_bytes);
    free(window->title);
    free(window);
    return 0;
}

uint32_t *hydrogen_window_surface_get(hydrogen_window_t *window) {
    if (!window) {
        return 0;
    }
    return window->surface;
}

int hydrogen_window_present(hydrogen_window_t *window) {
    if (!window) {
        return -1;
    }
    (void)window;
    return 0;
}

int hydrogen_window_poll_event(hydrogen_window_t *window,
                              hydrogen_window_event_t *event) {
    if (!window || !event) {
        return -1;
    }

    memset(event, 0, sizeof(*event));

    if (window->closed) {
        event->type = HYDROGEN_WINDOW_EVENT_CLOSE;
        return 1;
    }

    if (window->event_read_fd < 0) {
        return 0;
    }

    for (;;) {
        if (window->event_pending_len >= sizeof(*event)) {
            break;
        }

        long ready = poll_wait_fd(window->event_read_fd, POLL_READ, 0);
        if (!(ready & POLL_READ)) {
            if (ready & POLL_HUP) {
                window->closed = 1;
            }
            break;
        }

        size_t space = sizeof(*event) - window->event_pending_len;
        long n = read(window->event_read_fd,
                      window->event_pending + window->event_pending_len,
                      space);
        if (n <= 0) {
            if (n == 0) {
                window->closed = 1;
            }
            break;
        }
        window->event_pending_len += (size_t)n;
    }

    if (window->event_pending_len >= sizeof(*event)) {
        memcpy(event, window->event_pending, sizeof(*event));
        size_t consumed = sizeof(*event);
        size_t remaining = window->event_pending_len - consumed;
        if (remaining > 0) {
            shift_pending(window->event_pending, consumed, 0, remaining);
        }
        window->event_pending_len = remaining;
        return 1;
    }

    if (window->closed) {
        event->type = HYDROGEN_WINDOW_EVENT_CLOSE;
        return 1;
    }

    return 0;
}