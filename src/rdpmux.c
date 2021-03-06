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
    display_update *u = &update->disp_update;

    int new_x1 = x;
    int new_y1 = y;
    int new_x2 = x+w;
    int new_y2 = y+h;

    u->x1 = MIN(u->x1, new_x1);
    u->y1 = MIN(u->y1, new_y1);
    u->x2 = MAX(u->x2, new_x2);
    u->y2 = MAX(u->y2, new_y2);
}

/**
 * @func Copies a pixel region from one buffer to another. The two buffers are assumed to have the same subpixel
 * layout and bpp. The function will transfer a given rectangle of certain dimension from the source buffer to
 * a rectangle in the destination buffer with the same width and height, but not necessarily the same coordinates.
 *
 * @param dstData Pointer to the destination buffer. Assumed to be big enough to hold the data being copied into it.
 * @param dstStep Scanline of dstData.
 * @param xDst x-coordinate of the top-left corner of the destination rectangle.
 * @param yDst y-coordinate of the top-left corner of the destination rectangle.
 * @param width width of the rectangle in px.
 * @param height height of the rectangle in px.
 * @param srcData Pointer to the source buffer.
 * @param srcStep Scanline of the source buffer.
 * @param xSrc x-coordinate of the top-left corner of the source rectangle.
 * @param ySrc y-coordinate of the top-left corner of the source rectangle.
 * @param bpp Bits per pixel of the two buffers.
 */
static void mux_copy_pixels(unsigned char *dstData, int dstStep, int xDst, int yDst, int width, int height,
                            unsigned char *srcData, int srcStep, int xSrc, int ySrc, int bpp)
{
	int lineSize;
	int pixelSize;
	unsigned char* pSrc;
	unsigned char* pDst;
	unsigned char* pEnd;

	pixelSize = (bpp + 7) / 8;
	lineSize = width * pixelSize;

	pSrc = &srcData[(ySrc * srcStep) + (xSrc * pixelSize)];
	pDst = &dstData[(yDst * dstStep) + (xDst * pixelSize)];


    // when the source and destination rectangles are both strips
    // of the framebuffer spanning the full width, it's much cheaper
    // to do one memcpy rather than going line-by-line.
	if ((srcStep == dstStep) && (lineSize == srcStep)) {
		memcpy(pDst, pSrc, lineSize * height);
	} else {
		pEnd = pSrc + (srcStep * height);

		while (pSrc < pEnd) {
			memcpy(pDst, pSrc, lineSize);
			pSrc += srcStep;
			pDst += dstStep;
		}
	}
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
    mux_printf("DCL display update event triggered");
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

    mux_printf("Bounding box updated to [(%d, %d), (%d, %d)]", update->disp_update.x1, update->disp_update.x2,
           update->disp_update.x2, update->disp_update.y2);
}

/**
 * @func Public API function, to be called if the framebuffer surface changes in a user-facing way; for example, when the
 * display buffer resolution changes. In here, we create a new shared memory region for the framebuffer if necessary,
 * and do a straight memcpy of the new framebuffer data into the space. We then enqueue a display switch event that
 * contains the new shm region's information and the new dimensions of the display buffer. Finally, we notify the outside
 * about the new target framerate we'd like
 *
 * @param surface The new framebuffer display surface.
 *
 * @returns Target framerate for the VM guest.
 */
__PUBLIC void mux_display_switch(pixman_image_t *surface)
{
    mux_printf("DCL display switch event triggered.");

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
    int shm_size = 4096 * 2048 * sizeof(uint32_t); // rdp protocol has max framebuffer size 4096x2048
    sprintf(socket_str, socket_fmt, display->vm_id);

    // set up the shm region. This path only runs the first time a display switch event is received.
    if (display->shmem_fd < 0) {
        // this is the shm buffer being created! Hooray!
        int shim_fd = shm_open(socket_str,
                               O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IRGRP | S_IROTH);
        if (shim_fd < 0) {
            mux_printf_error("shm_open failed: %s", strerror(errno));
            return;
        }

        // resize the newly created shm region to the size of the framebuffer
        if (ftruncate(shim_fd, shm_size)) {
            mux_printf_error("ftruncate of new buffer failed: %s", strerror(errno));
            return;
        }
        // save our new shm file descriptor for later use
        display->shmem_fd = shim_fd;

        // mmap the shm region into our process space
        void *shm_buffer = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, display->shmem_fd, 0);
        if (shm_buffer == MAP_FAILED) {
            mux_printf_error("mmap failed: %s", strerror(errno));
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

    pthread_mutex_lock(&display->shm_lock);
    memcpy(display->shm_buffer, framebuf_data,
           width * height * sizeof(uint32_t));

    // signal the shm condition to wake up the processing loop
    pthread_cond_signal(&display->shm_cond);
    pthread_mutex_unlock(&display->shm_lock);


    // place our display switch update in the outgoing queue
    mux_queue_enqueue(&display->outgoing_messages, update);
    mux_printf("DISPLAY: DCL display switch callback completed successfully.");
}

/**
 * @func Public API function, to be called when the framebuffer display refreshes.
 *
 * This function attempts to lock the shared memory region, and if it succeeds, will sync the framebuffer
 * to the shared memory and copy the current dirty update for transmission.
 */
__PUBLIC uint32_t mux_display_refresh()
{
    if (display->dirty_update) {
        if (pthread_mutex_trylock(&display->shm_lock) == 0) {
            int pixelSize;
            size_t x = 0;
            size_t y = 0;
            size_t w = 0;
            size_t h = 0;
            display_update* u;
            size_t surfaceWidth = pixman_image_get_width(display->surface);
            size_t surfaceHeight = pixman_image_get_height(display->surface);
            int bpp = PIXMAN_FORMAT_BPP(pixman_image_get_format(display->surface));
            unsigned char* srcData = (unsigned char*) pixman_image_get_data(display->surface);
            unsigned char* dstData = (unsigned char*) display->shm_buffer;

            mux_printf("Now copying framebuffer to shmem region");

            u = &display->dirty_update->disp_update;

            // align the bounding box to 16 for memory alignment purposes
            if (u->x1 % 16) {
                u->x1 -= (u->x1 % 16);
            }

            if (u->y1 % 16) {
                u->y1 -= (u->y1 % 16);
            }

            if (u->x2 % 16) {
                u->x2 += 16 - (u->x2 % 16);
            }

            if (u->y2 % 16) {
                u->y2 += 16 - (u->y2 % 16);
            }

            if (u->x2 > surfaceWidth) {
                u->x2 = surfaceWidth;
            }

            if (u->y2 > surfaceHeight) {
                u->y2 = surfaceHeight;
            }

            y = u->y1;
            h = u->y2 - u->y1;

            pixelSize = (bpp + 7) / 8;

            // aligning the copy offsets does not yield a good performance gain,
            // but copying contiguous memory blocks makes a huge difference.
            // by forcing copying of full lines on buffers with the same step,
            // we can use a single memcpy rather than one memcpy per line.
            // this may over-copy a bit sometimes, but it's still way cheaper.
            x = 0;
            w = surfaceWidth;

            mux_copy_pixels(dstData, w * pixelSize, x, y, w, h, srcData, w * pixelSize, x, y, bpp);

            if (display->out_update == NULL) {
                mux_printf("Copying dirty update to out update");
                display->out_update = g_memdup(display->dirty_update, sizeof(MuxUpdate));
            } else {
                mux_printf("Calculating new out update bounds");
                mux_expand_rect(display->out_update, u->x1, u->y1, u->x2 - u->x1, u->y2 - u->y1);
            }

            g_free(display->dirty_update);
            display->dirty_update = NULL;

            pthread_cond_signal(&display->update_cond);
            pthread_mutex_unlock(&display->shm_lock);
        }
    } else {
//        mux_printf("Refresh deferred");
    }
    return (uint32_t) (1000 / display->framerate);
}

/*
 * Loops
 */

/**
 * @func Outgoing message loop. It is designed to be run as a thread runloop, and should be dispatched as a runnable
 * inside a separate thread during library initialization. Its function prototype matches what pthreads et al. expect.
 *
 * This function queues outgoing messages, and blocks waiting for the server to copy data out of the shared memory
 * region and return an ack message to the library. This ensures that the library and server are not accessing the shared
 * memory concurrently.
 */
__PUBLIC void mux_out_loop()
{
    while(true) {

        // check if exiting
        pthread_mutex_lock(&display->stop_lock);
        if (display->stop) break;
        pthread_mutex_unlock(&display->stop_lock);

        pthread_mutex_lock(&display->shm_lock);
        while (display->out_update == NULL) {

            // check if exiting
            pthread_mutex_lock(&display->stop_lock);
            if (display->stop) {
                goto cleanup;
            }
            pthread_mutex_unlock(&display->stop_lock);

            pthread_cond_wait(&display->update_cond, &display->shm_lock);
        }

        // check if exiting
        pthread_mutex_lock(&display->stop_lock);
        if (display->stop) break;
        pthread_mutex_unlock(&display->stop_lock);

        // place the update on the outgoing queue
        mux_queue_enqueue(&display->outgoing_messages, display->out_update);
        display->out_update = NULL;
        mux_printf("out_update qeueued and reset!");

        // block on the signal.
        mux_printf("Now waiting on ack from other process");
        pthread_cond_wait(&display->shm_cond, &display->shm_lock);

        // check if exiting
        pthread_mutex_lock(&display->stop_lock);
        if (display->stop) break;
        pthread_mutex_unlock(&display->stop_lock);

        mux_printf("Ack received! Unlocking shm region and continuing\n----------");
        pthread_mutex_unlock(&display->shm_lock);
    }
cleanup:
    pthread_mutex_unlock(&display->shm_lock);
    mux_printf("Now exiting out loop!");
    return;
}

/**
 * @func Unused, stubbed out until formal removal.
 *
 * @param arg Not at all used, just there to satisfy pthreads.
 */
__PUBLIC void *mux_display_buffer_update_loop(void *arg)
{
    return NULL;
}


static void mux_send_shutdown_msg()
{
    nnStr msg;
    msg.buf = NULL;
    size_t len = mux_write_outgoing_msg(NULL, &msg); // NULL means shutdown!
    while(mux_0mq_send_msg(msg.buf, len) < 0) {
        mux_printf_error("Failed to send shutdown message!");
    }
    g_free(msg.buf);
    mux_printf("Shutdown message sent!");
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
    mux_printf("Reached qemu shim in loop thread!");
    void *buf = NULL;
    size_t len;
    zpoller_t *poller = display->zmq.poller;
    bool stopping = false;

    // main shim receive loop
    int nbytes;
    while(!stopping) {
        nnStr msg;
        msg.buf = NULL;
        buf = NULL;

        while(!mux_queue_check_is_empty(&display->outgoing_messages)) {
            MuxUpdate *update = (MuxUpdate *) mux_queue_dequeue(&display->outgoing_messages); // blocks until something in queue
            len = mux_write_outgoing_msg(update, &msg); // serialize update to buf
            while (mux_0mq_send_msg(msg.buf, len) < 0) {
                mux_printf_error("Failed to send message");
            }
            g_free(update); // update is no longer needed, free it
            g_free(msg.buf); // free buf, no longer needed.
            msg.buf = NULL;
        }

        // block on receiving messages
        zsock_t *which = (zsock_t *) zpoller_wait(poller, 5); // 5ms timeout
        if (which != display->zmq.socket)  {
            if (zpoller_terminated(poller)) {
                mux_printf_error("Zpoller terminated!");
                stopping = true;
            }
        } else {
            nbytes = mux_0mq_recv_msg(&buf);
            if (nbytes > 0) {
                // successful recv is successful
                mux_printf("We have received a message of size %d bytes!", nbytes);
                mux_process_incoming_msg(buf, nbytes);
            }
        }

        pthread_mutex_lock(&display->stop_lock);
        if (display->stop) {
            stopping = true;
        }
        pthread_mutex_unlock(&display->stop_lock);
    }

    // cleanup
    mux_printf("Cleaning up!");
    pthread_mutex_unlock(&display->stop_lock);

    // clean up queues
    mux_queue_clear(&display->outgoing_messages);

    // send shutdown msg
    mux_send_shutdown_msg();

    // clean up socket
    if (!zpoller_terminated(display->zmq.poller)) {
        zpoller_destroy(&display->zmq.poller);
    }
//    zsock_set_linger(display->zmq.socket, 1);
    zsock_disconnect(display->zmq.socket, "%s", display->zmq.path);
    zsock_destroy(&display->zmq.socket);
    // signal to wake up other thread
    pthread_cond_signal(&display->update_cond);
    pthread_cond_signal(&display->shm_cond);
    return NULL;
}

/**
 * @func Initializes a queue.
 *
 * @param q The queue to initialize.
 */
static void mux_init_queue(MuxMsgQueue *q)
{
    SIMPLEQ_INIT(&q->updates);
    pthread_cond_init(&q->cond, NULL);
    pthread_mutex_init(&q->lock, NULL);
}

/**
 * @func This function initializes the data structures used by the library. It also returns a pointer to the ShimDisplay
 * struct initialized, which is defined as an opaque type in the public header so that client code can't mess with it.
 *
 * You must pass a string containing an UUID into the VM. This UUID will be used to uniquely identify the VM with the
 * frontend server, and will be passed in every message.
 *
 * @param uuid A UUID describing the VM.
 */
__PUBLIC MuxDisplay *mux_init_display_struct(const char *uuid)
{
    display = g_malloc0(sizeof(MuxDisplay));
    display->shmem_fd = -1;
    display->dirty_update = NULL;
    display->out_update = NULL;
    display->uuid = NULL;
    display->zmq.socket = NULL;
    display->framerate = 20;

    if (uuid != NULL) {
        if (strlen(uuid) != 36) {
            mux_printf_error("Invalid UUID");
            free(display);
            return NULL;
        }
        if ((display->uuid = strdup(uuid)) == NULL) {
            mux_printf_error("String copy failed: %s", strerror(errno));
            free(display);
            return NULL;
        }
    } else {
        display->uuid = NULL;
    }

    pthread_cond_init(&display->shm_cond, NULL);
    pthread_mutex_init(&display->shm_lock, NULL);
    pthread_cond_init(&display->update_cond, NULL);

    mux_init_queue(&display->outgoing_messages);

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
__PUBLIC void mux_cleanup(MuxDisplay *d)
{
    mux_printf("Now cleaning up librdpmux struct");
    if (d == NULL) {
        mux_printf_error("Invalid MuxDisplay pointer");
        return;
    }

    pthread_mutex_lock(&display->stop_lock);
    display->stop = true;
    pthread_mutex_unlock(&display->stop_lock);

    // clean up uuid
    g_free(&display->uuid);
}
