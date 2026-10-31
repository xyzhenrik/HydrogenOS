#pragma once

#include "anim.h"
#include "compositor.h"

/* The motion of one window between two poses. A pose is where the window is
   drawn on screen and how opaque. Opening, closing, minimizing to a taskbar
   icon, restoring, maximizing and the launcher appearing are all the same
   motion with different end poses, driven by one spring. */

#define MOTION_OPAQUE 255u

typedef struct MotionPose {
    CompRect rect;
    unsigned int opacity;   /* 0..255 */
} MotionPose;

typedef struct WindowMotion {
    AnimSpring progress;    /* 0: pose `from`, 1: pose `to` */
    MotionPose from;
    MotionPose to;
    int to_is_home;         /* `to` is the window's own rectangle, wherever it is by then */
    int active;
} WindowMotion;

/* The rectangle scaled about its centre by scale (16.16). */
CompRect motion_rect_scaled(const CompRect *rect, AnimValue scale);

/* A square of `size` pixels centred on (x, y). */
CompRect motion_rect_collapsed(int x, int y, int size);

/* Starts moving to `to`. An idle window begins at `from`; one that is already
   moving begins at the pose it has reached, so retargeting never jumps. With
   motion disabled nothing is started and the window is left where the caller
   puts it. `home` is the window's own rectangle. */
void window_motion_begin(WindowMotion *motion, const CompRect *home, const MotionPose *from,
                         const MotionPose *to, int to_is_home, AnimTuning tuning,
                         int motion_enabled);

/* Returns nonzero while the motion is still running. */
int window_motion_advance(WindowMotion *motion, uint32_t elapsed_ms);

/* The pose for the current frame; the end pose once the motion has stopped. */
MotionPose window_motion_pose(const WindowMotion *motion, const CompRect *home);

/* Stops at the end pose. */
void window_motion_finish(WindowMotion *motion);
