#pragma once

#include <stdint.h>

/* All values are fractions of full travel (0..1), except curve which is an exponent. */
typedef struct {
    float inner;  /* below this radius the stick reads as centered */
    float outer;  /* at or above this radius the stick reads as full */
    float anti;   /* output starts at this radius once past the inner deadzone */
    float curve;  /* 1 = linear, >1 = finer control near center */
} stick_cfg;

typedef struct {
    float inner;
    float outer;
} trigger_cfg;

typedef struct {
    stick_cfg left;
    stick_cfg right;
    trigger_cfg l2;
    trigger_cfg r2;
} deadzone_cfg;

/* Number of floats in the flat config array passed from Kotlin. Order matches DeadzoneConfig.toArray(). */
#define DEADZONE_CFG_FLOATS 12

void deadzone_cfg_default(deadzone_cfg *cfg);
void deadzone_cfg_from_array(deadzone_cfg *cfg, const float *values);

/* Radial deadzone on a 0..255 stick (center 128). */
void deadzone_apply_stick(const stick_cfg *cfg, uint8_t in_x, uint8_t in_y, uint8_t *out_x, uint8_t *out_y);

/* Linear deadzone on a 0..255 trigger. */
uint8_t deadzone_apply_trigger(const trigger_cfg *cfg, uint8_t in);
