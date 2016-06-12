/** @file */
#include <pixman.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "msgpack.h"
#include "0mq.h"
#include "queue.h"

InputEventCallbacks callbacks;
MuxDisplay *display;

/**
 * @func Copies the framebuffer into the shared memory region.
 *
 * @param update The update to copy out. Should be verified by caller to be a DISPLAY_UPDATE struct.
 */
static void mux_copy_update_to_shmem_region(MuxUpdate *update)
{
    // in this function, we copy the update's dirty buffer to the new shmem
    // region and place the update on the outgoing queue for transmission. We
    // then wait for a cond to signal before we release control.
    
    pthread_mutex_lock(&display->shm_lock);

    // place the update on the outgoing queue
    mux_queue_enqueue(&display->outgoing_messages, update);

    // block on the signal.
    // TODO: ensure this works because I'm not sure if this is guaranteed to wake up properly.
    printf("RDPMUX: Now waiting on ack from other process\n");
    pthread_cond_wait(&display->shm_cond, &display->shm_lock);
    printf("RDPMUX: Ack received! Unlocking shm region and continuing\n----------\n");
    pthread_mutex_unlock(&display->shm_lock);
}

/**
 * @func Checks whether the bounding box of the display update needs to be expanded, and does so if necessary.
 *
 * This function is called when more than one display update event is received per refresh tick. It computes the smallest
 * bounding box of the display buffer that contains all the updated screen regions received thus far, and updates the
 * display update with the new coordinates.
 *
 * @param update The update object to update if necessary. Should be verified to be of type DISPLAY_UPDATE by caller.
 * @param x The X-coordinate of the top-left corner of the new region to be included in the update.
 * @param y The Y-coordinate of the top-left corner of the new region to be included in the update.
 * @param w The width of the new region to be included in the update.
 * @param h The height of the new region to be included in the update.
 */
static void mux_expand_rect(MuxUpdate *update, int x, int y, int w, int h)
{
    display_update u = update->disp_update;

    int new_x1 = x;
    int new_y1 = y;
    int new_x2 = x+w;
    int new_y2 = y+h;

    int old_x1 = u.x1;
    int old_y1 = u.y1;
    int old_x2 = u.x2;
    int old_y2 = u.y2;

    u.x1 = MIN(old_x1, new_x1);
    u.y1 = MIN(old_y1, new_y1);
    u.x2 = MAX(old_x2, new_x2);
    u.y2 = MAX(old_y2, new_y2);

    printf("Bounding box updated to [(%d, %d), (%d, %d)]\n", u.x1, u.y1, u.x2, u.y2);
}

/**
 * @func Public API function designed to be called when a region of the framebuffer changes. For example, when a window
 * moves or an animation updates on screen.
 *
 * The function accepts four parameters [(x, y) w x h] that together define the rectangular bounding box of the changed
 * region in pixels.
 *
 * @param x X coordinate of the top-left corner of the changed region.
 * @param y Y-coordinate of the top-left corner of the changed region.
 * @param w Width of the changed region, in px.
 * @param h Height of the changed region, in px.
 */
__PUBLIC void mux_display_update(int x, int y, int w, int h)
{
    printf("RDPMUX: DCL display update event triggered.\n");
    MuxUpdate *update;
    if (!display->dirty_update) {
        update = g_malloc0(sizeof(MuxUpdate));
        update->type = DISPLAY_UPDATE;
        update->disp_update.x1 = x;
        update->disp_update.y1 = y;
        update->disp_update.x2 = x+w;
        update->disp_update.y2 = y+h;
        display->dirty_update = update;
    } else {
        // update dirty bounding box
        update = display->dirty_update;
        if (update->type != DISPLAY_UPDATE) {
            return;
        }
        mux_expand_rect(update, x, y, w, h);
    }

    printf("RDPMUX: DCL display update event (%d, %d) %dx%d completed successfully.\n",
                             x, y, w, h);
}

/**
 * @func Public API function, to be called if the framebuffer surface changes in a user-facing way. For example, when the
 * display buffer resolution changes.
 *
 * @param surface The new framebuffer display surface.
 */
__PUBLIC void mux_display_switch(pixman_image_t *surface)
{
    // This callback is fired whenever the dimensions of the display buffer
    // change in a user-facing noticeable way. In here, we create a new shared
    // memory region for the framebuffer if necessary, and do a straight memcpy
    // of the new framebuffer data into the space. We then enqueue a display
    // switch event that contains the new shm region's information and the new
    // dimensions of the display buffer.
    printf("RDPMUX: DCL display switch event triggered.\n");

    // save the pointers in our display struct for further use.
    display->surface = surface;
    uint32_t *framebuf_data = pixman_image_get_data(display->surface);
    int width = pixman_image_get_width(display->surface);
    int height = pixman_image_get_height(display->surface);

    // do all sorts of stuff to get the shmem region opened and ready
    const char *socket_fmt = "/%d.rdpmux";
    char socket_str[20] = ""; // 20 is a magic number carefully chosen
    // to be the length of INT_MAX plus the characters
    // in socket_fmt. If you change socket_fmt, make
    // sure to change this too.
    int shm_size = 4096 * 2048 * sizeof(uint32_t); // rdp protocol has max fb size 4096x2048
    sprintf(socket_str, socket_fmt, display->vm_id);

    // here begins the magic of setting up the shared memory region.
    // It truly is magical. Because I don't know why it works.
    // (j/k I know exactly why it works)
    // this path only runs the first time a display switch event is received.
    if (display->shmem_fd < 0) {
        // this is the shm buffer being created! Hooray!
        int shim_fd = shm_open(socket_str,
                               O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IRGRP | S_IROTH);
        if (shim_fd < 0) {
            printf("ERROR: shm_open failed: %s\n", strerror(errno));
            return;
        }

        // resize the newly created shm region to the size of the framebuffer
        if (ftruncate(shim_fd, shm_size)) {
            printf("ERROR: ftruncate of new buffer failed: %s\n", strerror(errno));
            return;
        }
        // save our new shm file descriptor for later use
        display->shmem_fd = shim_fd;

        // mmap the shm region into our process space
        void *shm_buffer = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, display->shmem_fd, 0);
        if (shm_buffer == MAP_FAILED) {
            printf("ERROR: mmap failed: %s\n", strerror(errno));
            return;
        }

        // save the pointer to the buffer for later use
        display->shm_buffer = shm_buffer;
    }

    // create the event update
    MuxUpdate *update = g_malloc0(sizeof(MuxUpdate));
    update->type = DISPLAY_SWITCH;
    update->disp_switch.shm_fd = display->shmem_fd;
    update->disp_switch.w = width;
    update->disp_switch.h = height;
    update->disp_switch.format = pixman_image_get_format(display->surface);

    // clear the outgoing queues (all those old messages for the old display
    // are now totally invalid since we have a new display to work against)
    mux_queue_clear(&display->outgoing_messages);
    mux_queue_clear(&display->display_buffer_updates);

    pthread_mutex_lock(&display->shm_lock);
    memcpy(display->shm_buffer, framebuf_data,
           width * height * sizeof(uint32_t));

    // signal the shm condition to wake up the processing loop
    pthread_cond_signal(&display->shm_cond);
    pthread_mutex_unlock(&display->shm_lock);


    // place our display switch update in the outgoing queue
    mux_queue_enqueue(&display->outgoing_messages, update);
    printf("DISPLAY: DCL display switch callback completed successfully.\n");
}

/**
 * @func Public API function, to be called when the framebuffer display refreshes.
 */
__PUBLIC void mux_display_refresh()
{
    if (display->dirty_update) {
        if (pthread_mutex_trylock(&display->shm_lock) == 0) {
            uint32_t *framebuf_data = pixman_image_get_data(display->surface);
            uint32_t *shm_data = (uint32_t *) display->shm_buffer;
            size_t w = pixman_image_get_width(display->surface);
            size_t h = pixman_image_get_height(display->surface);
            int bpp = PIXMAN_FORMAT_BPP(pixman_image_get_format(display->surface));

            printf("RDPMUX: Now copying framebuffer to shmem region\n");
            memcpy(shm_data, framebuf_data, w * h * (bpp / 8));

            mux_queue_enqueue(&display->display_buffer_updates, display->dirty_update);
            printf("RDPMUX: Refresh queued to RDP server process\n");
            display->dirty_update = NULL;
            pthread_mutex_unlock(&display->shm_lock);
        }
    } else {
        printf("RDPMUX: Refresh deferred\n");
    }
}

/*
 * Loops
 */

/**
 * @func Unused, stubbed out until formal removal.
 */
__PUBLIC void mux_out_loop()
{
    return;
}

/**
 * @func This function manages display buffer updates. It is designed to be a thread runloop, and should be
 * dispatched as a runnable inside a separate thread during library initialization. Its function prototype
 * matches what pthreads et al. expect.
 *
 * @param arg Not at all used, just there to satisfy pthreads.
 */
__PUBLIC void *mux_display_buffer_update_loop(void *arg)
{
    printf("RDPMUX: Reached shim display buffer update thread!\n");
    // init queue
    SIMPLEQ_INIT(&display->display_buffer_updates.updates);

    while(1) {
        MuxUpdate *update = (MuxUpdate *) mux_queue_dequeue(&display->display_buffer_updates);
        if (update->type == DISPLAY_UPDATE) {
            mux_copy_update_to_shmem_region(update); // will block until we receive ack
        } else {
            printf("ERROR: Wrong type of update queued on outgoing updates queue\n");
        }
    }
    return NULL;
}

/**
 * @func This function manages communication to and from the library. It is designed to be a thread runloop, and should
 * be dispatched as a runnable inside a separate thread during library initialization. Its function prototype
 * matches what pthreads et al. expect.
 *
 * @param arg void pointer to anything. Not used by the function in any way, just there to satisfy pthreads.
 */
__PUBLIC void *mux_mainloop(void *arg)
{
    printf("RDPMUX: Reached qemu shim in loop thread!\n");
    void *buf = NULL;
    size_t len;
    zpoller_t *poller = display->zmq.poller;

    // main shim receive loop
    int nbytes;
    while(1) {
        nnStr msg;
        msg.buf = NULL;
        buf = NULL;

        while(!mux_queue_check_is_empty(&display->outgoing_messages)) {
            MuxUpdate *update = (MuxUpdate *) mux_queue_dequeue(&display->outgoing_messages); // blocks until something in queue
            len = mux_write_outgoing_msg(update, &msg); // serialize update to buf
            while (mux_0mq_send_msg(msg.buf, len) < 0) {
                printf("ERROR: Failed to send message\n");
            }
            g_free(update); // update is no longer needed, free it
            g_free(msg.buf); // free buf, no longer needed.
            msg.buf = NULL;
        }

        // block on receiving messages
        zsock_t *which = (zsock_t *) zpoller_wait(poller, 5); // 5ms timeout
        if (which != display->zmq.socket)  {
            if (zpoller_terminated(poller)) {
                printf("ERROR: Zpoller terminated!\n");
                return NULL;
            }
        } else {
            nbytes = mux_0mq_recv_msg(&buf);
            if (nbytes > 0) {
                // successful recv is successful
                //printf("DEBUG: We have received a message of size %d bytes!\n", nbytes);
                mux_process_incoming_msg(buf, nbytes);
            }
        }
    }
    return NULL;
}

/**
 * @func Initializes a queue.
 *
 * @param q The queue to initialize.
 */
static void mux_init_queue(MuxMsgQueue *q)
{
    printf("RDPMUX: Now initializing a queue!\n");
    SIMPLEQ_INIT(&q->updates);
    pthread_cond_init(&q->cond, NULL);
    pthread_mutex_init(&q->lock, NULL);
}

/**
 * @func This function initializes the data structures used by the library. It also returns a pointer to the ShimDisplay
 * struct initialized, which is defined as an opaque type in the public header so that client code can't mess with it.
 *
 * @param uuid A UUID describing the VM.
 */
__PUBLIC MuxDisplay *mux_init_display_struct(const char *uuid)
{
    printf("RDPMUX: Now initializing display struct\n");
    display = g_malloc0(sizeof(MuxDisplay));
    display->shmem_fd = -1;
    display->dirty_update = NULL;
    display->uuid = NULL;
    display->zmq.socket = NULL;

    if (uuid != NULL) {
        if (strlen(uuid) != 36) {
            printf("ERROR: Invalid UUID passed to mux_init_display_struct()\n");
            free(display);
            return NULL;
        }
        if ((display->uuid = strdup(uuid)) == NULL) {
            printf("ERROR: String copy failed: %s\n", strerror(errno));
            free(display);
            return NULL;
        }
    } else {
        display->uuid = NULL;
    }

    pthread_cond_init(&display->shm_cond, NULL);
    pthread_mutex_init(&display->shm_lock, NULL);

    mux_init_queue(&display->outgoing_messages);
    mux_init_queue(&display->display_buffer_updates);

    return display;
}

/**
 * @func Register mouse and keyboard event callbacks using this function. The function pointers you register will be
 * called when mouse and keyboard events are received for you to handle and process.
 */
__PUBLIC void mux_register_event_callbacks(InputEventCallbacks cb)
{
    callbacks = cb;
}

/**
 * @func Should be called to safely cleanup library state. Note that ZeroMQ threads may (will) hang around for a long
 * time unless they're cleaned up by this method.
 */
__PUBLIC void mux_cleanup(MuxDisplay *display)
{
    printf("DEBUG: Now cleaning up librdpmux struct\n");
    if (display == NULL) {
        printf("ERROR: Invalid pointer passed to mux cleanup\n");
        return;
    }
    // clean up queues
    mux_queue_clear(&display->outgoing_messages);
    mux_queue_clear(&display->display_buffer_updates);

    // clean up uuid
    g_free(&display->uuid);

    // clean up socket
    zpoller_destroy(&display->zmq.poller);
    zsock_set_linger(&display->zmq.socket, 0);
    zsock_destroy(&display->zmq.socket);

    // clean up conds/mutexes
    pthread_cond_destroy(&display->shm_cond);
    pthread_mutex_destroy(&display->shm_lock);
}
