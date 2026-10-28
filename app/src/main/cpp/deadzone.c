#include "deadzone.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint8_t to_axis_byte(float v) {
    long b = lrintf(128.0f + v * 127.0f);
    return (uint8_t) (b < 0 ? 0 : (b > 255 ? 255 : b));
}

void deadzone_cfg_default(deadzone_cfg *cfg) {
    const stick_cfg stick = {.inner = 0.05f, .outer = 0.98f, .anti = 0.0f, .curve = 1.0f};
    const trigger_cfg trigger = {.inner = 0.0f, .outer = 1.0f};
    cfg->left = stick;
    cfg->right = stick;
    cfg->l2 = trigger;
    cfg->r2 = trigger;
}

static void stick_from_array(stick_cfg *s, const float *v) {
    s->inner = clampf(v[0], 0.0f, 0.95f);
    s->outer = clampf(v[1], s->inner + 0.01f, 1.0f);
    s->anti = clampf(v[2], 0.0f, 0.95f);
    s->curve = clampf(v[3], 0.2f, 5.0f);
}

static void trigger_from_array(trigger_cfg *t, const float *v) {
    t->inner = clampf(v[0], 0.0f, 0.95f);
    t->outer = clampf(v[1], t->inner + 0.01f, 1.0f);
}

void deadzone_cfg_from_array(deadzone_cfg *cfg, const float *values) {
    stick_from_array(&cfg->left, &values[0]);
    stick_from_array(&cfg->right, &values[4]);
    trigger_from_array(&cfg->l2, &values[8]);
    trigger_from_array(&cfg->r2, &values[10]);
}

void deadzone_apply_stick(const stick_cfg *cfg, uint8_t in_x, uint8_t in_y, uint8_t *out_x, uint8_t *out_y) {
    float x = clampf(((float) in_x - 128.0f) / 127.0f, -1.0f, 1.0f);
    float y = clampf(((float) in_y - 128.0f) / 127.0f, -1.0f, 1.0f);
    float r = sqrtf(x * x + y * y);

    if (r <= cfg->inner) {
        *out_x = 128;
        *out_y = 128;
        return;
    }

    /* Rescale (inner..outer) to (0..1) along the same direction so diagonals keep their angle. */
    float t = clampf((r - cfg->inner) / (cfg->outer - cfg->inner), 0.0f, 1.0f);
    t = powf(t, cfg->curve);
    t = cfg->anti + (1.0f - cfg->anti) * t;

    float scale = t / r;
    *out_x = to_axis_byte(clampf(x * scale, -1.0f, 1.0f));
    *out_y = to_axis_byte(clampf(y * scale, -1.0f, 1.0f));
}

uint8_t deadzone_apply_trigger(const trigger_cfg *cfg, uint8_t in) {
    float v = (float) in / 255.0f;
    if (v <= cfg->inner) return 0;
    float t = clampf((v - cfg->inner) / (cfg->outer - cfg->inner), 0.0f, 1.0f);
    return (uint8_t) lrintf(t * 255.0f);
}
