#include <asm-generic/errno-base.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include "wayland.h"

static uint32_t wayland_current_id = 1;

static const uint32_t wayland_display_object_id = 1;
static const uint16_t wayland_wl_registry_event_global = 0;
static const uint16_t wayland_shm_pool_event_format = 0;
static const uint16_t wayland_wl_buffer_event_release = 0;
static const uint16_t wayland_xdg_wm_base_event_ping = 0;
static const uint16_t wayland_xdg_toplevel_event_configure = 0;
static const uint16_t wayland_xdg_toplevel_event_close = 1;
static const uint16_t wayland_xdg_surface_event_configure = 0;
static const uint16_t wayland_wl_display_get_registry_opcode = 1;
static const uint16_t wayland_wl_registry_bind_opcode = 0;
static const uint16_t wayland_wl_compositor_create_surface_opcode = 0;
static const uint16_t wayland_xdg_wm_base_pong_opcode = 3;
static const uint16_t wayland_xdg_surface_ack_configure_opcode = 4;
static const uint16_t wayland_wl_shm_create_pool_opcode = 0;
static const uint16_t wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
static const uint16_t wayland_wl_shm_pool_create_buffer_opcode = 0;
static const uint16_t wayland_wl_surface_attach_opcode = 1;
static const uint16_t wayland_xdg_surface_get_toplevel_opcode = 1;
static const uint16_t wayland_wl_surface_commit_opcode = 6;
static const uint16_t wayland_wl_display_error_event = 0;
static const uint32_t wayland_format_xrgb8888 = 1;
static const uint32_t wayland_header_size = 8;
static const uint32_t color_channels = 4;

// Open a unix socket to the XDG runtime for our display
int wayland_display_connect() {
    char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir == NULL) 
        return EINVAL;

    uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    assert(xdg_runtime_dir_len <= cstring_len(addr.sun_path));
    uint64_t socket_path_len = 0;

    memcpy(addr.sun_path, xdg_runtime_dir, xdg_runtime_dir_len);
    socket_path_len += xdg_runtime_dir_len;

    addr.sun_path[socket_path_len++] = '/';

    char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display == NULL) {
        char wayland_display_default[] = "wayland-0";
        uint64_t wayland_display_default_len = cstring_len(wayland_display_default);
    
        memcpy(addr.sun_path + socket_path_len, wayland_display_default,
                wayland_display_default_len);
        socket_path_len += wayland_display_default_len;
    } else {
        uint64_t wayland_display_len = strlen(wayland_display);
        memcpy(addr.sun_path + socket_path_len, wayland_display, 
                wayland_display_len);
        socket_path_len += wayland_display_len;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) 
        exit(errno);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) 
        exit(errno);

    return fd;
}

static void buf_write_u16(char *buf, uint64_t *buf_len, uint64_t buf_cap, uint16_t x) {
    assert(*buf_len + sizeof(x) <= buf_cap);
    memcpy(buf + *buf_len, &x, sizeof(x));
    *buf_len += sizeof(x);
}

static void buf_write_u32(char *buf, uint64_t *buf_len, uint64_t buf_cap, uint32_t x) {
    assert(*buf_len + sizeof(x) <= buf_cap);
    memcpy(buf + *buf_len, &x, sizeof(x));
    *buf_len += sizeof(x);
}

static void buf_write_string(char *buf, uint64_t *buf_len, uint64_t buf_cap, char *src, uint32_t src_len) {
    assert(*buf_len + src_len <= buf_cap);

    buf_write_u32(buf, buf_len, buf_cap, src_len);
    memcpy(buf + *buf_len, src, roundup_4(src_len));
    *buf_len += roundup_4(src_len);
}

static uint32_t buf_read_u32(char **buf, uint64_t *buf_len) {
    assert(*buf_len >= sizeof(uint32_t));
    assert((size_t) *buf % sizeof(uint32_t) == 0);

    uint32_t res = *(uint32_t *)(*buf);
    *buf += sizeof(res);
    *buf_len -= sizeof(res);

    return res;
}

static uint32_t buf_read_u16(char **buf, uint64_t *buf_len) {
    assert(*buf_len >= sizeof(uint16_t));
    assert((size_t) *buf % sizeof(uint16_t) == 0);

    uint16_t res = *(uint16_t *)(*buf);
    *buf += sizeof(res);
    *buf_len -= sizeof(res);

    return res;
}

static void buf_read_n(char **buf, uint64_t *buf_len, char *dst, uint64_t n) {
    assert(*buf_len >= n);

    memcpy(dst, *buf, n);

    *buf += n;
    *buf_len -= n;
}

uint32_t wayland_wl_display_get_registry(int fd) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_display_object_id);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_display_get_registry_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_display@%u.get_registry: wl_registry=%u\n", wayland_display_object_id, wayland_current_id);

    return wayland_current_id;
}

static void hexdump(const char *label, const char *buf, uint64_t len) {
    printf("   %s(%" PRIu64 "):", label, len);
    for (uint64_t i = 0; i < len; i++) {
        printf("%02X", (unsigned char)buf[i]);
    }
    putchar('\n');
}

static uint32_t wayland_wl_registry_bind(int fd, uint32_t wl_registry, uint32_t name, char *interface, uint32_t interface_len, uint32_t version) {
    uint64_t msg_len = 0;
    char msg[512] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), wl_registry);
    hexdump("regid", msg, msg_len);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_registry_bind_opcode);

    uint64_t msg_announced_size = wayland_header_size + 
        sizeof(name) + sizeof(interface_len) + roundup_4(interface_len) + sizeof(version) +
        sizeof(wayland_current_id);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);
    hexdump("payload_size", msg, msg_len);

    buf_write_u32(msg, &msg_len, sizeof(msg), name);
    hexdump("name", msg, msg_len);
    buf_write_string(msg, &msg_len, sizeof(msg), interface, interface_len);
    hexdump("interface", msg, msg_len);
    buf_write_u32(msg, &msg_len, sizeof(msg), version);
    hexdump("version", msg, msg_len);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);
    hexdump("", msg, msg_len);
   
    printf("-> wl_registry@%u.bind: name=%u interface=%.*s version=%u =%u\n", wl_registry, name, interface_len, interface, version, wayland_current_id);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    return wayland_current_id;
}

uint32_t wayland_wl_compositor_create_surface(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_compositor);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_compositor_create_surface_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_compositor@%u.create_surface: =%u\n", state->wl_compositor, wayland_current_id);
    return wayland_current_id;
}

void wayland_wl_surface_attach(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_surface);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_surface_attach_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(state->wl_buffer) + sizeof(uint32_t) + sizeof(uint32_t);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_buffer);
    buf_write_u32(msg, &msg_len, sizeof(msg), 0);
    buf_write_u32(msg, &msg_len, sizeof(msg), 0);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_surface@%u.attach buffer=%u x=%u y=%u\n", state->wl_surface, state->wl_buffer, 0, 0);
}

void wayland_wl_surface_commit(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_surface);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_surface_commit_opcode);

    uint64_t msg_announced_size = wayland_header_size;
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_surface@%u.commit\n", state->wl_surface);
}

uint32_t wayland_wl_shm_create_pool(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_shm);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_shm_create_pool_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id) + sizeof(state->shm_fd) + sizeof(state->shm_pool_size);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);

    buf_write_u32(msg, &msg_len, sizeof(msg), state->shm_fd);
    buf_write_u32(msg, &msg_len, sizeof(msg), state->shm_pool_size);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_shm@%u.create_pool: =%u fd=%u size=%u\n", state->wl_shm, wayland_current_id, state->shm_fd, state->shm_pool_size);
    return wayland_current_id;
}

uint32_t wayland_wl_shm_pool_create_buffer(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_shm_pool);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_shm_pool_create_buffer_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id) + 
        sizeof(uint32_t) + sizeof(state->w) + sizeof(state->h) + sizeof(state->stride) + sizeof(wayland_format_xrgb8888);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);

    buf_write_u32(msg, &msg_len, sizeof(msg), 0); // offset
    buf_write_u32(msg, &msg_len, sizeof(msg), state->w); // width
    buf_write_u32(msg, &msg_len, sizeof(msg), state->h); // height
    buf_write_u32(msg, &msg_len, sizeof(msg), state->stride); // stride
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_format_xrgb8888); // format

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> wl_shm_pool@%u.create_buffer: =%u offset=%u width=%u height=%u stride=%u format=%u\n", 
            state->wl_shm, wayland_current_id, 0, state->w, state->h, state->stride, wayland_format_xrgb8888);
    return wayland_current_id;
}

uint32_t wayland_xdg_wm_base_get_xdg_surface(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->xdg_wm_base);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_xdg_wm_base_get_xdg_surface_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id) + sizeof(state->wl_surface);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);
    
    buf_write_u32(msg, &msg_len, sizeof(msg), state->wl_surface);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> xdg_wm_base@%u.get_xdg_surface: =%u surface=%u\n", state->xdg_wm_base, wayland_current_id, state->wl_surface);
    return wayland_current_id;
}

static void wayland_xdg_wm_base_pong(int fd, state_t *state, uint32_t ping) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->xdg_wm_base);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_wl_surface_commit_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(ping);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    buf_write_u32(msg, &msg_len, sizeof(msg), ping);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> xdg_wm_base@%u.pong ping=%u\n", state->xdg_wm_base, ping);
}

uint32_t wayland_xdg_surface_get_toplevel(int fd, state_t *state) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->xdg_surface);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_xdg_surface_get_toplevel_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(wayland_current_id);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    wayland_current_id++;
    buf_write_u32(msg, &msg_len, sizeof(msg), wayland_current_id);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> xdg_surface@%u.get_toplevel: =%u\n", state->xdg_surface, wayland_current_id);
    return wayland_current_id;
}

static void wayland_xdg_surface_ack_configure(int fd, state_t *state, uint32_t configure) {
    uint64_t msg_len = 0;
    char msg[128] = "";
    buf_write_u32(msg, &msg_len, sizeof(msg), state->xdg_surface);
    buf_write_u16(msg, &msg_len, sizeof(msg), wayland_xdg_surface_ack_configure_opcode);

    uint64_t msg_announced_size = wayland_header_size + sizeof(configure);
    assert(roundup_4(msg_announced_size) == msg_announced_size);
    buf_write_u16(msg, &msg_len, sizeof(msg), msg_announced_size);

    buf_write_u32(msg, &msg_len, sizeof(msg), configure);

    if ((int64_t)msg_len != send(fd, msg, msg_len, MSG_DONTWAIT))
        exit(errno);

    printf("-> xdg_surface@%u.ack_configure configure=%u\n", state->xdg_surface, configure);
}

void wayland_handle_message(int fd, state_t *state, char **msg, uint64_t *msg_len) {
    assert(*msg_len >= 8);

    uint32_t object_id = buf_read_u32(msg, msg_len);
    assert(object_id <= wayland_current_id);

    uint16_t opcode = buf_read_u16(msg, msg_len);

    uint16_t announced_size = buf_read_u16(msg, msg_len);
    assert(roundup_4(announced_size) <= announced_size);

    uint32_t header_size = sizeof(object_id) + sizeof(opcode) + sizeof(announced_size);
    assert(announced_size <= header_size + *msg_len);

    if (object_id == state->wl_registry &&
            opcode == wayland_wl_registry_event_global) {
        // TODO
    }

    if (object_id == state->wl_registry &&
            opcode == wayland_wl_registry_event_global) {
        uint32_t name = buf_read_u32(msg, msg_len);

        uint32_t interface_len = buf_read_u32(msg, msg_len);
        uint32_t padded_interface_len = roundup_4(interface_len);

        char interface[512] = "";
        assert(padded_interface_len <= cstring_len(interface));

        buf_read_n(msg, msg_len, interface, padded_interface_len);
        assert(interface[interface_len - 1] == 0);

        uint32_t version = buf_read_u32(msg, msg_len);

        printf("<- wl_registry@%u.global: name=%u interface=%.*s version=%u\n", 
                state->wl_registry, name, interface_len, interface, version);

        assert(announced_size == sizeof(object_id) + sizeof(announced_size) +
                                    sizeof(opcode) + sizeof(name) + 
                                    sizeof(interface_len) + padded_interface_len +
                                    sizeof(version));

        char wl_shm_interface[] = "wl_shm";
        if (strcmp(wl_shm_interface, interface) == 0) {
            state->wl_shm = wayland_wl_registry_bind(
                    fd, state->wl_registry, name, interface, interface_len, version);
        }

        char xdg_wm_base_interface[] = "xdg_wm_base";
        if (strcmp(xdg_wm_base_interface, interface) == 0) {
            state->xdg_wm_base = wayland_wl_registry_bind(
                    fd, state->wl_registry, name, interface, interface_len, version);
        }

        char wl_compositor_interface[] = "wl_compositor";
        if (strcmp(wl_compositor_interface, interface) == 0) {
            state->wl_compositor = wayland_wl_registry_bind(
                    fd, state->wl_registry, name, interface, interface_len, version);
        }

        return;
    } else if (object_id == state->xdg_wm_base && opcode == wayland_xdg_wm_base_event_ping) {
        uint32_t ping = buf_read_u32(msg, msg_len);
        printf("<- xdg_wm_base@%u.ping: ping=%u\n", state->xdg_wm_base, ping);
        wayland_xdg_wm_base_pong(fd, state, ping);
    } else if (object_id == state->xdg_surface && opcode == wayland_xdg_surface_event_configure) {
        uint32_t configure = buf_read_u32(msg, msg_len);
        printf("<- xdg_surface@%u.configre: configure=%u\n", state->xdg_surface, configure);
        wayland_xdg_surface_ack_configure(fd, state, configure);
        state->state = STATE_SURFACE_ACKED_CONFIGURE;
    } else if (object_id == wayland_display_object_id && opcode == wayland_wl_display_error_event) {
        uint32_t target_object_id = buf_read_u32(msg, msg_len);
        uint32_t code = buf_read_u32(msg, msg_len);
        char error[512] = "";
        uint32_t error_len = buf_read_u32(msg, msg_len);
        buf_read_n(msg, msg_len, error, roundup_4(error_len));

        fprintf(stderr, "fatal error: target_object_id=%u code=%u error=%s\n",
                target_object_id, code, error);
        exit(EINVAL);
    }
}
