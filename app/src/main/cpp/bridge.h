#pragma once

#include <stdint.h>

/* Live stick/trigger values for the UI. raw = from the real controller, out = sent to the virtual one. */
typedef struct {
    uint8_t raw_lx, raw_ly, raw_rx, raw_ry;
    uint8_t out_lx, out_ly, out_rx, out_ry;
    uint8_t raw_l2, raw_r2;
    uint8_t out_l2, out_r2;
} bridge_snapshot;

/* Finds the real DualSense, grabs it, creates the virtual one and starts the event thread.
 * Writes an error message to err and returns -1 on failure. */
int bridge_start(char *err, int err_len);
void bridge_stop(void);
int bridge_is_running(void);

void bridge_set_config(const float *values, int count);
void bridge_get_snapshot(bridge_snapshot *out);
