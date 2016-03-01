#ifndef SHIM_EXTERNAL_H
#define SHIM_EXTERNAL_H

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <glib.h>
#include <pixman.h>

typedef struct EventCallbacks {
    void (*shim_receive_kb)(uint32_t keycode, uint32_t flags);
    void (*shim_receive_mouse)(uint32_t x, uint32_t y, uint32_t flags);
} EventCallbacks;

typedef struct shim_display ShimDisplay;

void shim_display_update(int x, int y, int w, int h);
void shim_display_switch(pixman_image_t *surface);
void shim_display_refresh();

void *shim_mainloop(void *arg);
void shim_out_loop();
void *shim_display_buffer_update_loop(void *arg);

void shim_register_event_callbacks(EventCallbacks cb);
ShimDisplay *shim_init_display_struct();
bool shim_connect(const char *path);
bool shim_get_socket_path(const char *name, const char *obj, char **out_path, int id);

#endif //SHIM_EXTERNAL_H
