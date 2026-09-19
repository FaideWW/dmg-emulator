#include <stdint.h>

#define cstring_len(s) (sizeof(s)-1)
#define roundup_4(n) (((n)+3) & -4)

enum state_state_t {
    STATE_NONE,
    STATE_SURFACE_ACKED_CONFIGURE,
    STATE_SURFACE_ATTACHED,
};
typedef enum state_state_t state_state_t;

struct state_t {
    uint32_t wl_registry;
    uint32_t wl_shm;
    uint32_t wl_shm_pool;
    uint32_t wl_buffer;
    uint32_t xdg_wm_base;
    uint32_t xdg_surface;
    uint32_t wl_compositor;
    uint32_t wl_surface;
    uint32_t xdg_toplevel;
    uint32_t stride;
    uint32_t w;
    uint32_t h;
    uint32_t shm_pool_size;
    int shm_fd;
    uint8_t *shm_pool_data;

    state_state_t state;
};
typedef struct state_t state_t;

int wayland_display_connect();

uint32_t wayland_wl_display_get_registry(int fd);
uint32_t wayland_wl_compositor_create_surface(int fd, state_t *state);
uint32_t wayland_wl_shm_create_pool(int fd, state_t *state);
uint32_t wayland_wl_shm_pool_create_buffer(int fd, state_t *state);
void wayland_wl_surface_attach(int fd, state_t *state);
void wayland_wl_surface_commit(int fd, state_t *state);
uint32_t wayland_xdg_wm_base_get_xdg_surface(int fd, state_t *state);
uint32_t wayland_xdg_surface_get_toplevel(int fd, state_t *state);

void wayland_handle_message(int fd, state_t *state, char **msg, uint64_t *msg_len);
