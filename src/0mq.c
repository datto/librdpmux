/** @file */
#include "0mq.h"
#include "common.h"

/**
 * @brief Receives data through the given 0mq socket and sticks it into buf.
 *
 * This function is blocking.
 *
 * @returns Number of bytes read.
 *
 * @param buf Pointer to a void* buffer.
 */
int mux_0mq_recv_msg(void **buf)
{
    zframe_t *msg;
    if ((msg = zframe_recv(display->zmq.socket)) == NULL) {
        printf("RDPMUX: 0mq: Something went wrong with the recv operation\n");
        return -1;
    }
    //printf("LIBSHIM: Receiving message through nn sock now\n");
    return 0;
}

/**
 * @brief Send a message through the 0mq socket.
 *
 * This function is blocking.
 *
 * @returns The number of bytes sent.
 *
 * @param buf The data to send.
 * @param len The length of buf.
 */
int mux_0mq_send_msg(void *buf, size_t len)
{
    zframe_t *message;
    assert(buf != NULL);
    message = zframe_new(buf, len);
    printf("RDPMUX: Sending message through 0mq sock now!\n");
    if (zframe_send(&message, display->zmq.socket, 0) == -1) {
        printf("ERROR: Could not send message through 0mq socket\n");
        return -1;
    }
    return 0;
}

/**
 * @brief Connects to the 0mq socket on path.
 *
 * Connects to the 0mq socket located on the file path passed in, then stores that socket in the global display struct
 * upon success.
 *
 * @returns Whether the connection succeeded.
 *
 * @param path The path to the 0mq socket in the filesystem.
 */
__PUBLIC bool mux_connect(const char *path)
{
    display->zmq.socket = zsock_new_dealer(path);
    assert (display->zmq.socket != NULL);

    zmq_setsockopt(display->zmq.socket, ZMQ_IDENTITY, display->uuid, strlen(display->uuid));
    if (zsock_connect(display->zmq.socket, path) == -1) {
        printf("ERROR: 0mq connect failed");
        return false;
    }
    printf("RDPMUX: Bound to %s\n", path);
    return true;
}
