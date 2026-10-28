#include "ds_report.h"

#include <string.h>

/*
 * Minimal descriptor declaring the reports hid-playstation exchanges over USB.
 * The driver binds by VID/PID and parses raw bytes itself (HID_CONNECT_HIDRAW only),
 * so the descriptor just needs each report ID present with the right size.
 */
const uint8_t ds_report_descriptor[] = {
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x05,             /* Usage (Game Pad) */
    0xA1, 0x01,             /* Collection (Application) */
    0x06, 0x00, 0xFF,       /*   Usage Page (Vendor 0xFF00) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,       /*   Logical Maximum (255) */
    0x75, 0x08,             /*   Report Size (8) */

    0x85, DS_INPUT_REPORT_USB,
    0x09, 0x20,
    0x95, DS_INPUT_REPORT_USB_SIZE - 1,
    0x81, 0x02,             /*   Input (Data,Var,Abs) */

    0x85, DS_OUTPUT_REPORT_USB,
    0x09, 0x21,
    0x95, DS_OUTPUT_REPORT_USB_SIZE - 1,
    0x91, 0x02,             /*   Output (Data,Var,Abs) */

    0x85, DS_FEATURE_REPORT_CALIBRATION,
    0x09, 0x22,
    0x95, DS_FEATURE_REPORT_CALIBRATION_SIZE - 1,
    0xB1, 0x02,             /*   Feature (Data,Var,Abs) */

    0x85, DS_FEATURE_REPORT_PAIRING_INFO,
    0x09, 0x23,
    0x95, DS_FEATURE_REPORT_PAIRING_INFO_SIZE - 1,
    0xB1, 0x02,

    0x85, DS_FEATURE_REPORT_FIRMWARE_INFO,
    0x09, 0x24,
    0x95, DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE - 1,
    0xB1, 0x02,

    0xC0                    /* End Collection */
};
const size_t ds_report_descriptor_size = sizeof(ds_report_descriptor);

static void put_le16(uint8_t *p, int v) {
    p[0] = (uint8_t) (v & 0xFF);
    p[1] = (uint8_t) ((v >> 8) & 0xFF);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static int clamp_s16(int v) {
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

void ds_state_init(ds_state *s) {
    memset(s, 0, sizeof(*s));
    s->lx = s->ly = s->rx = s->ry = 128;
    /* Discharging, level 9 -> the driver reports 95%. TODO: mirror the real controller's battery. */
    s->battery_status = 0x09;
}

static uint8_t hat_value(int x, int y) {
    static const uint8_t table[3][3] = {
        /* x=-1  x=0          x=1 */
        {7, 0, 1},           /* y=-1 (up) */
        {6, DS_HAT_NONE, 2}, /* y=0 */
        {5, 4, 3},           /* y=1 (down) */
    };
    if (x < -1 || x > 1 || y < -1 || y > 1) return DS_HAT_NONE;
    return table[y + 1][x + 1];
}

static void put_touch(uint8_t *p, const ds_touch_point *t) {
    p[0] = t->active ? (uint8_t) (t->id & 0x7F) : 0x80;
    p[1] = (uint8_t) (t->x & 0xFF);
    p[2] = (uint8_t) (((t->x >> 8) & 0x0F) | ((t->y & 0x0F) << 4));
    p[3] = (uint8_t) ((t->y >> 4) & 0xFF);
}

void ds_build_input_report(const ds_state *s, uint8_t seq, uint8_t out[DS_INPUT_REPORT_USB_SIZE]) {
    memset(out, 0, DS_INPUT_REPORT_USB_SIZE);
    out[0] = DS_INPUT_REPORT_USB;
    out[1] = s->lx;
    out[2] = s->ly;
    out[3] = s->rx;
    out[4] = s->ry;
    out[5] = s->l2;
    out[6] = s->r2;
    out[7] = seq;

    out[8] = (uint8_t) (hat_value(s->hat_x, s->hat_y)
                        | (s->square ? 0x10 : 0) | (s->cross ? 0x20 : 0)
                        | (s->circle ? 0x40 : 0) | (s->triangle ? 0x80 : 0));
    out[9] = (uint8_t) ((s->l1 ? 0x01 : 0) | (s->r1 ? 0x02 : 0)
                        | (s->l2_btn ? 0x04 : 0) | (s->r2_btn ? 0x08 : 0)
                        | (s->create ? 0x10 : 0) | (s->options ? 0x20 : 0)
                        | (s->l3 ? 0x40 : 0) | (s->r3 ? 0x80 : 0));
    out[10] = (uint8_t) ((s->ps ? 0x01 : 0) | (s->touch_click ? 0x02 : 0));

    /* gyro[3] then accel[3], little-endian int16 */
    for (int i = 0; i < 3; i++) {
        put_le16(&out[16 + i * 2], clamp_s16(s->gyro[i] / DS_GYRO_RAW_DIVISOR));
        put_le16(&out[22 + i * 2], clamp_s16(s->accel[i]));
    }
    /* The driver reads the timestamp in units of 1/3 microsecond. */
    put_le32(&out[28], s->sensor_timestamp_us * 3u);

    put_touch(&out[33], &s->touch[0]);
    put_touch(&out[37], &s->touch[1]);

    out[53] = s->battery_status;
}

/*
 * Calibration chosen so the driver's conversion reproduces the evdev values we read:
 *   accel: bias 0, numer 2*8192, denom 16384      -> identity
 *   gyro:  bias 0, numer 2*1024, denom 16+16 = 32 -> x64, matching DS_GYRO_RAW_DIVISOR
 */
static void build_calibration(uint8_t *b) {
    /* gyro pitch/yaw/roll bias */
    put_le16(&b[1], 0);
    put_le16(&b[3], 0);
    put_le16(&b[5], 0);
    /* gyro pitch/yaw/roll plus, minus */
    for (int i = 0; i < 3; i++) {
        put_le16(&b[7 + i * 4], 16);
        put_le16(&b[9 + i * 4], -16);
    }
    /* gyro speed plus, minus */
    put_le16(&b[19], 1);
    put_le16(&b[21], 1);
    /* accel x/y/z plus, minus */
    for (int i = 0; i < 3; i++) {
        put_le16(&b[23 + i * 4], 8192);
        put_le16(&b[25 + i * 4], -8192);
    }
}

size_t ds_build_feature_report(uint8_t report_id, const uint8_t mac[6], uint8_t *out, size_t out_cap) {
    size_t size;
    switch (report_id) {
        case DS_FEATURE_REPORT_CALIBRATION: size = DS_FEATURE_REPORT_CALIBRATION_SIZE; break;
        case DS_FEATURE_REPORT_PAIRING_INFO: size = DS_FEATURE_REPORT_PAIRING_INFO_SIZE; break;
        case DS_FEATURE_REPORT_FIRMWARE_INFO: size = DS_FEATURE_REPORT_FIRMWARE_INFO_SIZE; break;
        default: return 0;
    }
    if (size > out_cap) return 0;

    memset(out, 0, size);
    out[0] = report_id;
    switch (report_id) {
        case DS_FEATURE_REPORT_CALIBRATION:
            build_calibration(out);
            break;
        case DS_FEATURE_REPORT_PAIRING_INFO:
            /* The driver stores bytes 1..6 as the MAC (reversed for display) and uses it as uniq. */
            memcpy(&out[1], mac, 6);
            break;
        case DS_FEATURE_REPORT_FIRMWARE_INFO:
            put_le32(&out[24], 0x00000315); /* hardware version */
            put_le32(&out[28], 0x0110002A); /* firmware version */
            put_le16(&out[44], 0x0224);     /* update version: >= 2.21 selects vibration v2 */
            break;
    }
    return size;
}

int ds_parse_output_rumble(const uint8_t *data, size_t size, uint8_t *left, uint8_t *right) {
    if (size < 40 || data[0] != DS_OUTPUT_REPORT_USB) return 0;
    const uint8_t *common = &data[1];
    uint8_t flag0 = common[0];
    uint8_t flag2 = common[38];
    /* COMPATIBLE_VIBRATION (flag0 bit0) or COMPATIBLE_VIBRATION2 (flag2 bit2) */
    if (!(flag0 & 0x01) && !(flag2 & 0x04)) return 0;
    *right = common[2];
    *left = common[3];
    return 1;
}
