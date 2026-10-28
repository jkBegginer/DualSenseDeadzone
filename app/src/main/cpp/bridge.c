#include "bridge.h"

#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uhid.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "deadzone.h"
#include "ds_report.h"

#define TAG "dsdz"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

#define VIRTUAL_NAME "DualSense Wireless Controller"
#define VIRTUAL_PHYS "dsdz/virtual"

/* Must differ from the real controller's MAC: hid-playstation rejects duplicate MACs. */
static const uint8_t VIRTUAL_MAC[6] = {0x02, 0xD5, 0xDE, 0xAD, 0x20, 0x01};

enum { NODE_GAMEPAD, NODE_MOTION, NODE_TOUCHPAD, NODE_COUNT };

typedef struct {
    int fds[NODE_COUNT];
    int uhid_fd;
    int wake_fd;
    pthread_t thread;
    atomic_int running;

    pthread_mutex_t lock; /* guards cfg and snapshot */
    deadzone_cfg cfg;
    bridge_snapshot snapshot;

    /* event-thread only */
    ds_state raw;
    int touch_slot;
    uint8_t seq;
    int rumble_warned;
    int rescan_hits; /* consecutive rescans that saw the controller while waiting for it */
} bridge_t;

static bridge_t g = {
    .fds = {-1, -1, -1},
    .uhid_fd = -1,
    .wake_fd = -1,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .snapshot = {
        .raw_lx = 128, .raw_ly = 128, .raw_rx = 128, .raw_ry = 128,
        .out_lx = 128, .out_ly = 128, .out_rx = 128, .out_ry = 128,
    },
};
static int g_cfg_initialized = 0;

static void set_err(char *err, int len, const char *fmt, const char *detail) {
    if (err && len > 0) snprintf(err, (size_t) len, fmt, detail);
}

/* ---------- device discovery ---------- */

/* hid-playstation leaves phys empty but sets uniq to the MAC from feature report 0x09, printed reversed. */
static int is_virtual(int fd) {
    char uniq[32] = {0};
    char ours[18];
    if (ioctl(fd, EVIOCGUNIQ(sizeof(uniq) - 1), uniq) < 0) return 0;
    snprintf(ours, sizeof(ours), "%02x:%02x:%02x:%02x:%02x:%02x",
             VIRTUAL_MAC[5], VIRTUAL_MAC[4], VIRTUAL_MAC[3], VIRTUAL_MAC[2], VIRTUAL_MAC[1], VIRTUAL_MAC[0]);
    return strcasecmp(uniq, ours) == 0;
}

static int node_kind(const char *name) {
    size_t n = strlen(name);
    const char *motion = " Motion Sensors";
    const char *touch = " Touchpad";
    if (n > strlen(motion) && strcmp(name + n - strlen(motion), motion) == 0) return NODE_MOTION;
    if (n > strlen(touch) && strcmp(name + n - strlen(touch), touch) == 0) return NODE_TOUCHPAD;
    return NODE_GAMEPAD;
}

/* Returns 0 when the gamepad node was found; *denied counts nodes we were not allowed to open. */
static int find_devices(int fds[NODE_COUNT], int *denied) {
    *denied = 0;
    DIR *dir = opendir("/dev/input");
    if (!dir) return -1;

    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);

        /* Read-only: shell's SELinux domain may deny write on input_device. EVIOCGRAB works without it. */
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            if (errno == EACCES || errno == EPERM) (*denied)++;
            continue;
        }

        struct input_id id;
        char name[128] = {0};
        if (ioctl(fd, EVIOCGID, &id) < 0 || id.vendor != DS_VENDOR_ID || id.product != DS_PRODUCT_ID
            || is_virtual(fd)) {
            close(fd);
            continue;
        }
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

        int kind = node_kind(name);
        if (fds[kind] >= 0) {
            /* TODO: support more than one controller; for now keep the first one found. */
            close(fd);
            continue;
        }
        fds[kind] = fd;
        LOGI("found %s: %s", path, name);
    }
    closedir(dir);
    return fds[NODE_GAMEPAD] >= 0 ? 0 : -1;
}

static void close_fds(int fds[NODE_COUNT]) {
    for (int i = 0; i < NODE_COUNT; i++) {
        if (fds[i] >= 0) {
            ioctl(fds[i], EVIOCGRAB, 0);
            close(fds[i]);
            fds[i] = -1;
        }
    }
}

static void close_devices(void) {
    close_fds(g.fds);
}

/* Grabs every open node so Android stops receiving the real controller's events. */
static int grab_fds(int fds[NODE_COUNT]) {
    for (int i = 0; i < NODE_COUNT; i++) {
        if (fds[i] >= 0 && ioctl(fds[i], EVIOCGRAB, 1) < 0) return -1;
    }
    return 0;
}

static int abs_value(int fd, int code, int fallback) {
    struct input_absinfo info;
    return ioctl(fd, EVIOCGABS(code), &info) < 0 ? fallback : info.value;
}

static void read_initial_state(void) {
    int fd = g.fds[NODE_GAMEPAD];
    g.raw.lx = (uint8_t) abs_value(fd, ABS_X, 128);
    g.raw.ly = (uint8_t) abs_value(fd, ABS_Y, 128);
    g.raw.rx = (uint8_t) abs_value(fd, ABS_RX, 128);
    g.raw.ry = (uint8_t) abs_value(fd, ABS_RY, 128);
    g.raw.l2 = (uint8_t) abs_value(fd, ABS_Z, 0);
    g.raw.r2 = (uint8_t) abs_value(fd, ABS_RZ, 0);
}

/* ---------- uhid ---------- */

static int uhid_write(const struct uhid_event *ev) {
    ssize_t n = write(g.uhid_fd, ev, sizeof(*ev));
    return n == (ssize_t) sizeof(*ev) ? 0 : -1;
}

static int uhid_create(void) {
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    snprintf((char *) ev.u.create2.name, sizeof(ev.u.create2.name), "%s", VIRTUAL_NAME);
    snprintf((char *) ev.u.create2.phys, sizeof(ev.u.create2.phys), "%s", VIRTUAL_PHYS);
    memcpy(ev.u.create2.rd_data, ds_report_descriptor, ds_report_descriptor_size);
    ev.u.create2.rd_size = (uint16_t) ds_report_descriptor_size;
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = DS_VENDOR_ID;
    ev.u.create2.product = DS_PRODUCT_ID;
    ev.u.create2.version = 0x0100;
    return uhid_write(&ev);
}

static void uhid_destroy(void) {
    if (g.uhid_fd < 0) return;
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_DESTROY;
    uhid_write(&ev);
    close(g.uhid_fd);
    g.uhid_fd = -1;
}

/* Opens /dev/uhid and registers a fresh virtual DualSense. The event loop answers the driver's probe. */
static int uhid_open_and_create(void) {
    g.uhid_fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (g.uhid_fd < 0) return -1;
    if (uhid_create() < 0) {
        int saved = errno;
        close(g.uhid_fd);
        g.uhid_fd = -1;
        errno = saved;
        return -1;
    }
    return 0;
}

static void send_input_report(void) {
    deadzone_cfg cfg;
    pthread_mutex_lock(&g.lock);
    cfg = g.cfg;
    pthread_mutex_unlock(&g.lock);

    ds_state out = g.raw;
    deadzone_apply_stick(&cfg.left, g.raw.lx, g.raw.ly, &out.lx, &out.ly);
    deadzone_apply_stick(&cfg.right, g.raw.rx, g.raw.ry, &out.rx, &out.ry);
    out.l2 = deadzone_apply_trigger(&cfg.l2, g.raw.l2);
    out.r2 = deadzone_apply_trigger(&cfg.r2, g.raw.r2);
    /* Keep the digital trigger bits consistent with the adjusted analog value. */
    if (out.l2 == 0) out.l2_btn = 0;
    if (out.r2 == 0) out.r2_btn = 0;

    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT2;
    ev.u.input2.size = DS_INPUT_REPORT_USB_SIZE;
    ds_build_input_report(&out, g.seq++, ev.u.input2.data);
    if (uhid_write(&ev) < 0) LOGW("uhid input write failed: %s", strerror(errno));

    pthread_mutex_lock(&g.lock);
    g.snapshot = (bridge_snapshot) {
        .raw_lx = g.raw.lx, .raw_ly = g.raw.ly, .raw_rx = g.raw.rx, .raw_ry = g.raw.ry,
        .out_lx = out.lx, .out_ly = out.ly, .out_rx = out.rx, .out_ry = out.ry,
        .raw_l2 = g.raw.l2, .raw_r2 = g.raw.r2, .out_l2 = out.l2, .out_r2 = out.r2,
    };
    pthread_mutex_unlock(&g.lock);
}

static void handle_uhid_event(void) {
    struct uhid_event ev;
    ssize_t n = read(g.uhid_fd, &ev, sizeof(ev));
    if (n <= 0) return;

    switch (ev.type) {
        case UHID_GET_REPORT: {
            struct uhid_event reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = UHID_GET_REPORT_REPLY;
            reply.u.get_report_reply.id = ev.u.get_report.id;
            size_t size = ds_build_feature_report(ev.u.get_report.rnum, VIRTUAL_MAC,
                                                  reply.u.get_report_reply.data,
                                                  sizeof(reply.u.get_report_reply.data));
            if (size == 0) {
                LOGW("unknown feature report 0x%02x", ev.u.get_report.rnum);
                reply.u.get_report_reply.err = EIO;
            } else {
                reply.u.get_report_reply.size = (uint16_t) size;
            }
            uhid_write(&reply);
            break;
        }
        case UHID_SET_REPORT: {
            struct uhid_event reply;
            memset(&reply, 0, sizeof(reply));
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id = ev.u.set_report.id;
            uhid_write(&reply);
            break;
        }
        case UHID_OUTPUT: {
            /*
             * TODO: rumble, lightbar, LEDs and adaptive triggers cannot reach the real pad here:
             * shell may not write evdev (SELinux) or open hidraw, and the grab blocks
             * FF playback injected through other handles (e.g. system_server).
             */
            uint8_t left, right;
            if (ds_parse_output_rumble(ev.u.output.data, ev.u.output.size, &left, &right)
                && (left || right) && !g.rumble_warned) {
                LOGW("rumble requested but forwarding is not supported on this device");
                g.rumble_warned = 1;
            }
            break;
        }
        case UHID_START:
            LOGI("virtual DualSense started");
            break;
        default:
            break;
    }
}

/* ---------- evdev -> state ---------- */

static void set_key(ds_state *s, int code, int v) {
    switch (code) {
        case BTN_SOUTH: s->cross = v; break;
        case BTN_EAST: s->circle = v; break;
        case BTN_NORTH: s->triangle = v; break;
        case BTN_WEST: s->square = v; break;
        case BTN_TL: s->l1 = v; break;
        case BTN_TR: s->r1 = v; break;
        case BTN_TL2: s->l2_btn = v; break;
        case BTN_TR2: s->r2_btn = v; break;
        case BTN_SELECT: s->create = v; break;
        case BTN_START: s->options = v; break;
        case BTN_THUMBL: s->l3 = v; break;
        case BTN_THUMBR: s->r3 = v; break;
        case BTN_MODE: s->ps = v; break;
        default: break;
    }
}

static void handle_gamepad(const struct input_event *e) {
    if (e->type == EV_KEY) {
        set_key(&g.raw, e->code, e->value != 0);
    } else if (e->type == EV_ABS) {
        switch (e->code) {
            case ABS_X: g.raw.lx = (uint8_t) e->value; break;
            case ABS_Y: g.raw.ly = (uint8_t) e->value; break;
            case ABS_RX: g.raw.rx = (uint8_t) e->value; break;
            case ABS_RY: g.raw.ry = (uint8_t) e->value; break;
            case ABS_Z: g.raw.l2 = (uint8_t) e->value; break;
            case ABS_RZ: g.raw.r2 = (uint8_t) e->value; break;
            case ABS_HAT0X: g.raw.hat_x = e->value; break;
            case ABS_HAT0Y: g.raw.hat_y = e->value; break;
            default: break;
        }
    }
}

static void handle_motion(const struct input_event *e) {
    if (e->type == EV_ABS) {
        switch (e->code) {
            case ABS_X: g.raw.accel[0] = e->value; break;
            case ABS_Y: g.raw.accel[1] = e->value; break;
            case ABS_Z: g.raw.accel[2] = e->value; break;
            case ABS_RX: g.raw.gyro[0] = e->value; break;
            case ABS_RY: g.raw.gyro[1] = e->value; break;
            case ABS_RZ: g.raw.gyro[2] = e->value; break;
            default: break;
        }
    } else if (e->type == EV_MSC && e->code == MSC_TIMESTAMP) {
        g.raw.sensor_timestamp_us = (uint32_t) e->value;
    }
}

static void handle_touchpad(const struct input_event *e) {
    if (e->type == EV_KEY && e->code == BTN_LEFT) {
        g.raw.touch_click = e->value != 0;
        return;
    }
    if (e->type != EV_ABS) return;
    if (e->code == ABS_MT_SLOT) {
        g.touch_slot = e->value;
        return;
    }
    if (g.touch_slot < 0 || g.touch_slot > 1) return;
    ds_touch_point *t = &g.raw.touch[g.touch_slot];
    switch (e->code) {
        case ABS_MT_TRACKING_ID:
            t->active = e->value >= 0;
            if (t->active) t->id = e->value;
            break;
        case ABS_MT_POSITION_X: t->x = e->value; break;
        case ABS_MT_POSITION_Y: t->y = e->value; break;
        default: break;
    }
}

/* Returns 1 if a SYN_REPORT was seen, -1 if the device went away. */
static int drain_node(int kind) {
    struct input_event events[64];
    int synced = 0;
    for (;;) {
        ssize_t n = read(g.fds[kind], events, sizeof(events));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) break;
            return -1;
        }
        if (n == 0) return -1;
        size_t count = (size_t) n / sizeof(struct input_event);
        for (size_t i = 0; i < count; i++) {
            const struct input_event *e = &events[i];
            if (e->type == EV_SYN) {
                if (e->code == SYN_REPORT) synced = 1;
                continue;
            }
            switch (kind) {
                case NODE_GAMEPAD: handle_gamepad(e); break;
                case NODE_MOTION: handle_motion(e); break;
                case NODE_TOUCHPAD: handle_touchpad(e); break;
            }
        }
    }
    return synced;
}

/* ---------- reconnect ---------- */

#define RESCAN_INTERVAL_MS 500

/*
 * When the controller drops (e.g. turned off) we release it and rescan until it is back.
 * The virtual pad is recreated right away, while the real pad is gone: Android hands out the
 * lowest free controller number, so the new virtual pad becomes controller #1 and the real pad
 * gets #2 when it returns. Games that bind controller #1 (e.g. Dead Cells) then use the virtual one.
 */
static void on_controller_lost(void) {
    LOGW("controller disconnected, recreating virtual pad and waiting for it to come back");
    close_devices();
    uint8_t battery = g.raw.battery_status;
    ds_state_init(&g.raw);
    g.raw.battery_status = battery;
    g.touch_slot = 0;
    g.rescan_hits = 0;

    uhid_destroy();
    if (uhid_open_and_create() < 0) {
        LOGW("recreating virtual pad failed: %s", strerror(errno));
        atomic_store(&g.running, 0);
    }
}

static void try_reacquire(void) {
    int fds[NODE_COUNT] = {-1, -1, -1};
    int denied = 0;
    if (find_devices(fds, &denied) < 0) {
        close_fds(fds);
        g.rescan_hits = 0;
        return;
    }
    /* The kernel creates the gamepad, motion and touchpad nodes one after another;
     * wait one more interval so we pick up all three. */
    if (g.rescan_hits++ == 0) {
        close_fds(fds);
        return;
    }
    g.rescan_hits = 0;
    if (grab_fds(fds) < 0) {
        LOGW("EVIOCGRAB failed on reconnect: %s", strerror(errno));
        close_fds(fds);
        return;
    }
    memcpy(g.fds, fds, sizeof(fds));
    g.touch_slot = 0;
    read_initial_state();
    LOGI("controller reconnected");
}

/* ---------- event thread ---------- */

static void *event_loop(void *arg) {
    (void) arg;
    struct pollfd pfds[NODE_COUNT + 2];

    while (atomic_load(&g.running)) {
        int waiting = g.fds[NODE_GAMEPAD] < 0;
        int n = 0;
        int kinds[NODE_COUNT];
        for (int i = 0; i < NODE_COUNT; i++) {
            if (g.fds[i] < 0) continue;
            pfds[n] = (struct pollfd) {.fd = g.fds[i], .events = POLLIN};
            kinds[n] = i;
            n++;
        }
        int uhid_idx = n;
        pfds[n++] = (struct pollfd) {.fd = g.uhid_fd, .events = POLLIN};
        int wake_idx = n;
        pfds[n++] = (struct pollfd) {.fd = g.wake_fd, .events = POLLIN};

        int ready = poll(pfds, (nfds_t) n, waiting ? RESCAN_INTERVAL_MS : -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOGW("poll failed: %s", strerror(errno));
            break;
        }
        if (pfds[wake_idx].revents) break;
        if (pfds[uhid_idx].revents & POLLIN) handle_uhid_event();
        if (waiting) {
            if (ready == 0) try_reacquire();
            continue;
        }

        /* One real report updates all three nodes; coalesce them into one virtual report. */
        int dirty = 0;
        int lost = 0;
        for (int i = 0; i < uhid_idx; i++) {
            if (!(pfds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
            int r = drain_node(kinds[i]);
            if (r < 0) {
                lost = 1;
                break;
            }
            dirty |= r;
        }
        if (lost) {
            on_controller_lost();
        } else if (dirty) {
            send_input_report();
        }
    }

    uhid_destroy();
    close_devices();
    atomic_store(&g.running, 0);
    LOGI("event loop stopped");
    return NULL;
}

/* ---------- public API ---------- */

int bridge_start(char *err, int err_len) {
    if (atomic_load(&g.running)) return 0;
    /* A previous loop may have exited on its own (controller unplugged); reap it. */
    if (g.wake_fd >= 0) bridge_stop();

    pthread_mutex_lock(&g.lock);
    if (!g_cfg_initialized) {
        deadzone_cfg_default(&g.cfg);
        g_cfg_initialized = 1;
    }
    pthread_mutex_unlock(&g.lock);

    for (int i = 0; i < NODE_COUNT; i++) g.fds[i] = -1;
    int denied = 0;
    if (find_devices(g.fds, &denied) < 0) {
        close_devices();
        if (denied > 0) {
            set_err(err, err_len, "%s", "Permission denied opening /dev/input (SELinux)");
        } else {
            set_err(err, err_len, "%s", "DualSense not found in /dev/input (connect it first)");
        }
        LOGW("start failed: %s", err);
        return -1;
    }

    if (grab_fds(g.fds) < 0) {
        set_err(err, err_len, "EVIOCGRAB failed: %s", strerror(errno));
        close_devices();
        return -1;
    }

    ds_state_init(&g.raw);
    read_initial_state();
    g.touch_slot = 0;
    g.seq = 0;
    g.rumble_warned = 0;

    g.uhid_fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (g.uhid_fd < 0) {
        set_err(err, err_len, "open /dev/uhid failed: %s", strerror(errno));
        close_devices();
        return -1;
    }
    if (uhid_create() < 0) {
        set_err(err, err_len, "UHID_CREATE2 failed: %s", strerror(errno));
        uhid_destroy();
        close_devices();
        return -1;
    }

    g.wake_fd = eventfd(0, EFD_CLOEXEC);
    atomic_store(&g.running, 1);
    /* The driver probes right after CREATE2 and waits for our feature replies, so start reading now. */
    if (pthread_create(&g.thread, NULL, event_loop, NULL) != 0) {
        atomic_store(&g.running, 0);
        set_err(err, err_len, "%s", "pthread_create failed");
        close(g.wake_fd);
        g.wake_fd = -1;
        uhid_destroy();
        close_devices();
        return -1;
    }
    LOGI("bridge started");
    return 0;
}

void bridge_stop(void) {
    if (g.wake_fd < 0) return;
    uint64_t one = 1;
    write(g.wake_fd, &one, sizeof(one));
    pthread_join(g.thread, NULL);
    close(g.wake_fd);
    g.wake_fd = -1;
}

int bridge_is_running(void) {
    return atomic_load(&g.running);
}

void bridge_set_config(const float *values, int count) {
    if (count < DEADZONE_CFG_FLOATS) return;
    pthread_mutex_lock(&g.lock);
    deadzone_cfg_from_array(&g.cfg, values);
    g_cfg_initialized = 1;
    pthread_mutex_unlock(&g.lock);
}

void bridge_get_snapshot(bridge_snapshot *out) {
    pthread_mutex_lock(&g.lock);
    *out = g.snapshot;
    pthread_mutex_unlock(&g.lock);
}
