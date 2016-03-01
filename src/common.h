/** @file */

#ifndef SHIM_COMMON_H
#define SHIM_COMMON_H

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <glib.h>
#include <pixman.h>

#include "lib/libqueue.h"
#include "lib/c-msgpack.h"

/**
 * @brief Used to define publicly available functions.
 */
#define __PUBLIC __attribute__((visibility("default")))

/**
 * @brief Protocol version.
 */
#define RDPMUX_PROTOCOL_VERSION 1

/**
 * @brief This struct is populated by the code using the library to provide callbacks for mouse and keyboard events.
 *
 * This struct is also exposed in the public header. The implementing code (usually the hypervisor) needs to provide
 * functions to deal with these events and register them into the library using shim_register_event_callbacks().
 */
typedef struct EventCallbacks {
    void (*shim_receive_kb)(uint32_t keycode, uint32_t flags);
    void (*shim_receive_mouse)(uint32_t x, uint32_t y, uint32_t flags);
} EventCallbacks;

/**
 * @brief The possible types of messages.
 */
typedef enum message_type {
    DISPLAY_UPDATE,
    DISPLAY_SWITCH,
    MOUSE,
    KEYBOARD,
    DISPLAY_UPDATE_COMPLETE,
    SHUTDOWN
} MessageType;

/**
 * @brief Parameters for a display update event.
 *
 * Display updates carry coordinates for a rectangular screen region, denoted as the coordinates of the top left corner
 * along with the width and height of the rectangle. All values are in px.
 */
typedef struct display_update {
    /**
     * @brief X-coordinate of top left corner of region
     */
    int x;
    /**
     * @brief Y-coordinate of top left corner of region
     */
    int y;
    /**
     * @brief width of region
     */
    int w;
    /**
     * @brief height of region
     */
    int h;
} display_update;

/**
 * @brief Parameters for a display switch event.
 */
typedef struct display_switch {
    /**
     * @brief file descriptor for the shared memory region.
     */
    int shm_fd;
    /**
     * @brief format code describing the pixel layout of the framebuffer.
     */
    pixman_format_code_t format;
    /**
     * @brief width of framebuffer in px.
     */
    int w;
    /**
     * @brief height of framebuffer in px.
     */
    int h;
} display_switch;

/**
 * @brief Parameters for a keyboard event.
 */
typedef struct kb_update {
    /**
     * @brief flags associated with the event. Could be anything, really. Commonly used for things like keyup/keydown
     * events and any special flags associated with the keypress.
     */
    uint32_t flags;
    /**
     * @brief The keycode. Could be anything, depends on what the client side application sends.
     */
    uint32_t keycode;
} kb_update;

/**
 * @brief Parameters for a mouse event.
 */
typedef struct mouse_update {
    /**
     * @brief flags associated with the mouse update event. Usually used to denote extended buttons and/or what type
     * of mouse event it is (movement, click, etc.)
     */
    uint32_t flags;
    /**
     * @brief X-coordinate of the mouse cursor in px.
     */
    uint32_t x;
    /**
     * @brief Y-coordinate of the mouse cursor in px.
     */
    uint32_t y;
} mouse_update;

/**
 * @brief Parameters for a update ack event.
 */
typedef struct update_ack {
    /**
     * @brief whether the update succeeded.
     */
    bool success;
} update_ack;

/**
 * @brief Object to hold information about the various types of events.
 *
 * The ShimUpdate struct can can hold exactly one of one type of event. They are designed to be held in queues.
 * Use the type field to determine what sort of event it is, rather than any other sort of hackery.
 */
typedef struct ShimUpdate {
    /**
     * @brief pointer to next update in queue.
     */
    SIMPLEQ_ENTRY(ShimUpdate) next;
    /**
     * @brief what kind of update this is.
     */
    MessageType type;
    /**
     * @brief union of the various event types.
     */
    union {
        display_update disp_update;
        display_switch disp_switch;
        kb_update kb;
        mouse_update mouse;
        update_ack ack;
    };
} ShimUpdate;

/**
 * @brief queue to hold ShimUpdate objects.
 *
 * This is a very simple queue backed by a linked list. Nothing fancy, gets the job done.
 */
typedef struct ShimMsgQueue {
    pthread_mutex_t lock;
    pthread_cond_t cond; // signals when not empty
    SIMPLEQ_HEAD(, ShimUpdate) updates;
} ShimMsgQueue;

/**
 * @brief Main struct
 *
 * This struct holds all of the state in the library. There is exactly one instance of this, initialized by
 * shim_init_display_struct() and stored as a global variable.
 */
struct shim_display {
    /**
     * @brief pointer to the framebuffer surface in memory.
     */
    pixman_image_t *surface;
    /**
     * @brief ID of the virtual machine
     */
    int vm_id;
    /**
     * @brief File descriptor of the shared memory region.
     */
    int shmem_fd;
    /**
     * @brief pointer to the shared memory region.
     */
    void *shm_buffer;
    /**
     * @brief Current dirty update
     */
    ShimUpdate *dirty_update;
    /**
     * @brief Nanomsg socket descriptor.
     */
    int nn_sock;

    /**
     * @brief Condition variable associated with shared memory region.
     */
    pthread_cond_t shm_cond;
    /**
     * @brief Lock guarding access to shared memory region.
     */
    pthread_mutex_t shm_lock;

    /**
     * @brief Outgoing message queue.
     */
    ShimMsgQueue outgoing_messages;
    /**
     * @brief Display buffer update queue.
     */
    ShimMsgQueue display_buffer_updates;
};
typedef struct shim_display ShimDisplay;

/**
 * @brief global variable of the registered EventCallbacks.
 */
extern EventCallbacks callbacks;

/**
 * @brief Global display struct.
 */
extern ShimDisplay *display;

#endif //SHIM_COMMON_H
