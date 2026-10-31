#include "window_motion.h"

static int lerp_int(int a, int b, AnimValue t) {
    return a + (int)(((int64_t)(b - a) * t + ANIM_ONE / 2) >> 16);
}

static int at_least_one(int value) {
    return value < 1 ? 1 : value;
}

CompRect motion_rect_scaled(const CompRect *rect, AnimValue scale) {
    CompRect out;
    out.w = at_least_one((int)(((int64_t)rect->w * scale + ANIM_ONE / 2) >> 16));
    out.h = at_least_one((int)(((int64_t)rect->h * scale + ANIM_ONE / 2) >> 16));
    out.x = rect->x + (rect->w - out.w) / 2;
    out.y = rect->y + (rect->h - out.h) / 2;
    return out;
}

CompRect motion_rect_collapsed(int x, int y, int size) {
    CompRect out = { x - size / 2, y - size / 2, at_least_one(size), at_least_one(size) };
    return out;
}

static MotionPose pose_between(const MotionPose *from, const MotionPose *to, AnimValue t) {
    MotionPose pose;
    pose.rect.x = lerp_int(from->rect.x, to->rect.x, t);
    pose.rect.y = lerp_int(from->rect.y, to->rect.y, t);
    pose.rect.w = at_least_one(lerp_int(from->rect.w, to->rect.w, t));
    pose.rect.h = at_least_one(lerp_int(from->rect.h, to->rect.h, t));
    pose.opacity = (unsigned int)lerp_int((int)from->opacity, (int)to->opacity, t);
    return pose;
}

MotionPose window_motion_pose(const WindowMotion *motion, const CompRect *home) {
    MotionPose to = motion->to;
    if (motion->to_is_home) to.rect = *home;
    if (!motion->active) return to;
    return pose_between(&motion->from, &to, anim_clamp_unit(motion->progress.value));
}

void window_motion_begin(WindowMotion *motion, const CompRect *home, const MotionPose *from,
                         const MotionPose *to, int to_is_home, AnimTuning tuning,
                         int motion_enabled) {
    MotionPose start = motion->active ? window_motion_pose(motion, home) : *from;
    motion->from = start;
    motion->to = *to;
    motion->to_is_home = to_is_home;
    if (!motion_enabled) {
        motion->active = 0;
        return;
    }
    anim_spring_init(&motion->progress, 0, tuning);
    anim_spring_go(&motion->progress, ANIM_ONE, 1);
    motion->active = 1;
}

int window_motion_advance(WindowMotion *motion, uint32_t elapsed_ms) {
    if (!motion->active) return 0;
    if (anim_spring_advance(&motion->progress, elapsed_ms)) return 1;
    motion->active = 0;
    return 0;
}

void window_motion_finish(WindowMotion *motion) {
    motion->active = 0;
}
