#pragma once

/* Animation engine of the desktop: critically damped springs on 16.16
   fixed-point values, advanced by the frame clock.

   A spring has no duration. It is retargeted at any moment and keeps its
   velocity, so an interrupted animation continues from where it is instead of
   jumping. The integration runs in fixed sub-steps whatever the frame time,
   so a slow frame advances the animation further rather than slowing it
   down, and the result does not depend on the frame rate. No floating point
   is involved. */

#include <stdint.h>

typedef int32_t AnimValue;

#define ANIM_ONE 65536   /* 1.0 */

/* Longest frame time one advance accounts for; a longer stall (a suspended
   session, a very slow frame) does not fast-forward past it. */
#define ANIM_MAX_ELAPSED_MS 250

/* Natural frequency in rad/s: the spring settles in about 8 / omega seconds. */
typedef struct AnimTuning {
    int32_t omega;
} AnimTuning;

/* Windows opening, closing, minimizing, moving between geometries. */
#define ANIM_TUNING_WINDOW  ((AnimTuning){ 24 })
/* Small controls: hover highlights, indicators. */
#define ANIM_TUNING_CONTROL ((AnimTuning){ 40 })
/* Panels that appear under the pointer, such as the launcher. */
#define ANIM_TUNING_PANEL   ((AnimTuning){ 32 })

typedef struct AnimSpring {
    AnimValue value;
    AnimValue target;
    int32_t velocity;       /* in AnimValue per second */
    uint32_t omega;
    uint32_t residual_ms;   /* time not yet consumed by a whole sub-step */
} AnimSpring;

/* A spring at rest at value. */
void anim_spring_init(AnimSpring *spring, AnimValue value, AnimTuning tuning);

/* Moves the target; value and velocity carry on. With motion disabled
   (reduce motion) the spring jumps to the target instead. */
void anim_spring_go(AnimSpring *spring, AnimValue target, int motion_enabled);

/* Jumps to value and stops. */
void anim_spring_snap(AnimSpring *spring, AnimValue value);

/* Lets elapsed_ms pass. Returns nonzero while the spring is still moving; once
   it is within imperceptible distance of the target at imperceptible speed it
   lands on the target exactly and stays there. */
int anim_spring_advance(AnimSpring *spring, uint32_t elapsed_ms);

int anim_spring_settled(const AnimSpring *spring);

/* a + (b - a) * t for t in 0..ANIM_ONE; t beyond that extrapolates. */
AnimValue anim_lerp(AnimValue a, AnimValue b, AnimValue t);

/* Nearest whole number / clamped to 0..ANIM_ONE. */
int anim_round(AnimValue value);
AnimValue anim_clamp_unit(AnimValue value);

/* Time between frames from a monotonic millisecond clock. */
typedef struct AnimClock {
    unsigned long last_ms;
    int started;
} AnimClock;

/* Milliseconds since the previous call (0 on the first), at most
   ANIM_MAX_ELAPSED_MS. A clock that went backwards counts as no time passed. */
uint32_t anim_clock_advance(AnimClock *clock, unsigned long now_ms);
