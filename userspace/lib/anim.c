#include "anim.h"

/* Integration step. The explicit-Euler discretization of a critically damped
   spring has real eigenvalues for every omega * step, so it does not
   oscillate; 4 ms keeps it within a fraction of a percent of the exact curve
   at the frequencies used here. */
#define STEP_MS 4

/* Rest: closer than a quarter of a thousandth of a unit (a tenth of a pixel on
   a 400 px travel) and slower than that per second. */
#define REST_DISTANCE 16
#define REST_SPEED    256

void anim_spring_init(AnimSpring *spring, AnimValue value, AnimTuning tuning) {
    spring->value = value;
    spring->target = value;
    spring->velocity = 0;
    spring->omega = tuning.omega > 0 ? (uint32_t)tuning.omega : 1u;
    spring->residual_ms = 0;
}

void anim_spring_snap(AnimSpring *spring, AnimValue value) {
    spring->value = value;
    spring->target = value;
    spring->velocity = 0;
    spring->residual_ms = 0;
}

void anim_spring_go(AnimSpring *spring, AnimValue target, int motion_enabled) {
    if (!motion_enabled) {
        anim_spring_snap(spring, target);
        return;
    }
    spring->target = target;
}

int anim_spring_settled(const AnimSpring *spring) {
    return spring->value == spring->target && spring->velocity == 0;
}

static int64_t absolute(int64_t value) {
    return value < 0 ? -value : value;
}

int anim_spring_advance(AnimSpring *spring, uint32_t elapsed_ms) {
    if (anim_spring_settled(spring)) {
        spring->residual_ms = 0;
        return 0;
    }
    if (elapsed_ms > ANIM_MAX_ELAPSED_MS) elapsed_ms = ANIM_MAX_ELAPSED_MS;

    uint32_t pending = spring->residual_ms + elapsed_ms;
    int64_t omega = spring->omega;
    int64_t stiffness = omega * omega;
    int64_t damping = 2 * omega;
    int64_t value = spring->value;
    int64_t velocity = spring->velocity;

    for (; pending >= STEP_MS; pending -= STEP_MS) {
        int64_t error = value - spring->target;
        int64_t acceleration = -stiffness * error - damping * velocity;
        velocity += acceleration * STEP_MS / 1000;
        value += velocity * STEP_MS / 1000;
        if (absolute(value - spring->target) <= REST_DISTANCE && absolute(velocity) <= REST_SPEED) {
            spring->value = spring->target;
            spring->velocity = 0;
            spring->residual_ms = 0;
            return 0;
        }
    }
    spring->value = (AnimValue)value;
    spring->velocity = (int32_t)velocity;
    spring->residual_ms = pending;
    return 1;
}

AnimValue anim_lerp(AnimValue a, AnimValue b, AnimValue t) {
    return (AnimValue)(a + ((int64_t)b - a) * t / ANIM_ONE);
}

int anim_round(AnimValue value) {
    return (int)((value + ANIM_ONE / 2) >> 16);
}

AnimValue anim_clamp_unit(AnimValue value) {
    if (value < 0) return 0;
    return value > ANIM_ONE ? ANIM_ONE : value;
}

uint32_t anim_clock_advance(AnimClock *clock, unsigned long now_ms) {
    uint32_t elapsed = 0;
    if (clock->started && now_ms > clock->last_ms) {
        unsigned long delta = now_ms - clock->last_ms;
        elapsed = delta > ANIM_MAX_ELAPSED_MS ? ANIM_MAX_ELAPSED_MS : (uint32_t)delta;
    }
    clock->last_ms = now_ms;
    clock->started = 1;
    return elapsed;
}
