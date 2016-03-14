/** @file */
#include "nanomsg.h"

/**
 * @brief Receives data through the given nanomsg socket and sticks it into buf.
 *
 * This function is blocking.
 *
 * @returns Number of bytes read.
 *
 * @param sock The nanomsg socket to block on.
 * @param buf Pointer to a void* buffer.
 */
int mux_nn_recv_msg(int sock, void **buf)
{
    int num = nn_recv(sock, buf, NN_MSG, 0);
    if (num < 0) {
        // TODO: EAGAIN indicates that the connection to the server timed out,
        // we should cleanup and cleanly exit instead of ignoring it.
        if (nn_errno() != EAGAIN) {
            printf("NANOMSG: Something went wrong with the recv operation\n");
            nn_freemsg(buf);
        }
        return -1;
    }
    //printf("LIBSHIM: Receiving message through nn sock now\n");
    return num;
}

/**
 * @brief Send a message through the nanomsg socket.
 *
 * This function is blocking.
 *
 * @returns The number of bytes sent.
 *
 * @param sock The nanomsg socket to send through.
 * @param buf The data to send.
 * @param len The length of buf.
 */
int mux_nn_send_msg(int sock, void *buf, size_t len)
{
    assert(buf != NULL);
    //printf("LIBSHIM: Sending message through nn sock now!\n");
    return nn_send(sock, buf, len, 0);
}

/**
 * @brief Connects to the nanomsg socket on path.
 *
 * Connects to the nanomsg socket located on the file path passed in, then stores that socket in the global display struct
 * upon success.
 *
 * @returns Whether the connection succeeded.
 *
 * @param path The path to the nanomsg socket in the filesystem.
 */
__PUBLIC bool mux_connect(const char *path)
{
    int to = 10000;
    int sock = nn_socket(AF_SP, NN_PAIR);
    assert (sock >= 0);

    nn_setsockopt(sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to));
    int endpoint = nn_connect(sock, path);
    if (endpoint < 0) {
        printf("ERROR: nn_connect failed: %s\n", nn_strerror(nn_errno()));
        return false;
    }
    display->nn_sock = sock;
    return true;
}
