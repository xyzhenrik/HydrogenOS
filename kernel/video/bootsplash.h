#pragma once

#include <stddef.h>
#include <stdint.h>

/* Boot splash: a wordmark and a thin progress bar on the framebuffer that
   hides the kernel log (still mirrored on serial) until the desktop presents
   its first full frame. The splash owns the screen while it is active. */

/* Memory management comes up before the display, so the splash is first drawn
   at PCI. */
typedef enum bootsplash_stage {
    BOOTSPLASH_STAGE_PCI = 0,
    BOOTSPLASH_STAGE_STORAGE,
    BOOTSPLASH_STAGE_FILESYSTEM,
    BOOTSPLASH_STAGE_INIT,
} bootsplash_stage_t;

typedef enum bootsplash_state {
    BOOTSPLASH_DISABLED = 0,
    BOOTSPLASH_ACTIVE,
    BOOTSPLASH_FINISHED,
} bootsplash_state_t;

/* Draws the splash with the progress of `stage`. With `enabled` clear, or
   without a 32-bit framebuffer, the splash stays disabled and the kernel
   console keeps the screen. */
void bootsplash_start(bootsplash_stage_t stage, int enabled);

/* Advances the progress bar to `stage`; progress never moves backwards. */
void bootsplash_stage(bootsplash_stage_t stage);

/* Applies one progress report from init (see docs/BOOT_SPLASH.md). Returns 0
   or a negative errno for a malformed report. */
int bootsplash_report(const uint8_t *data, uint32_t size);

/* Removes the splash and gives the screen back to the kernel console. */
void bootsplash_abort(void);

bootsplash_state_t bootsplash_state(void);
uint32_t bootsplash_progress(void);

/* Screen-update filter for the framebuffer syscalls. Returns 1 if the splash
   consumed the blit (it did not cover the whole screen, so the splash stays),
   else 0 and the caller blits as usual. A blit that covers the whole screen
   ends the splash by fading into the new frame first. */
int bootsplash_filter_blit(int x, int y, int w, int h, const uint32_t *src,
                           size_t stride_bytes);
