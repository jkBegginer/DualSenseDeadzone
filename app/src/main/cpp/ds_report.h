#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * DualSense USB wire formats, as parsed by the Linux hid-playstation driver.
 * Offsets are into the full report buffer, where byte 0 is the report ID.
 */

#define DS_VENDOR_ID  0x054C
#define DS_PRODUCT_ID 0x0CE6

#define DS_INPUT_REPORT_USB       0x01
#define DS_INPUT_REPORT_USB_SIZE  64
#define DS_OUTPUT_REPORT_USB      0x02
#define DS_OUTPUT_REPORT_USB_SIZE 63

#define DS_FEATURE_REPORT_CALIBRATION        0x05
#define DS_FEATURE_REPORT_CALIBRATION_SIZE   41
#define DS_FEATURE_REPORT_PAIRING_INFO       0x09
#define DS_FEATURE_REPORT_PAIRING_INFO_SIZE  20
#define DS_FEATURE_REPORT_FIRMWARE_INFO      0x20
#define DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE 64

/* Gyro values in the report are the evdev value divided by this (see ds_build_calibration). */
#define DS_GYRO_RAW_DIVISOR 64

/* Hat values for buttons[0] low nibble. */
#define DS_HAT_NONE 8

typedef struct {
    int active;
    int id;
    int x; /* 0..1919 */
    int y; /* 0..1079 */
} ds_touch_point;

/* Everything the input report carries, in evdev units. */
typedef struct {
    uint8_t lx, ly, rx, ry; /* 0..255, center 128 */
    uint8_t l2, r2;         /* 0..255 */
    int hat_x, hat_y;       /* -1..1 */

    /* digital buttons */
    int square, cross, circle, triangle;
    int l1, r1, l2_btn, r2_btn;
    int create, options, l3, r3;
    int ps, touch_click;

    /* motion sensors: accel 8192/g, gyro 1024 per deg/s */
    int accel[3];
    int gyro[3];
    uint32_t sensor_timestamp_us;

    ds_touch_point touch[2];

    uint8_t battery_status; /* raw status byte: high nibble charging state, low nibble level */
} ds_state;

extern const uint8_t ds_report_descriptor[];
extern const size_t ds_report_descriptor_size;

void ds_state_init(ds_state *s);

/* Builds the 64-byte USB input report 0x01. */
void ds_build_input_report(const ds_state *s, uint8_t seq, uint8_t out[DS_INPUT_REPORT_USB_SIZE]);

/* Fills a feature report reply. Returns its size, or 0 for an unknown report ID. */
size_t ds_build_feature_report(uint8_t report_id, const uint8_t mac[6], uint8_t *out, size_t out_cap);

/*
 * Reads rumble motor strengths from an output report sent by the driver.
 * Returns 1 and sets left/right (0..255) when the report carries a vibration update.
 */
int ds_parse_output_rumble(const uint8_t *data, size_t size, uint8_t *left, uint8_t *right);
