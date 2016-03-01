//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_NANOMSG_H
#define SHIM_NANOMSG_H

#include "common.h"
#include <nanomsg/nn.h>
#include <nanomsg/pair.h>

//void shim_process_incoming_msg(void *buf, int nbytes);
//size_t shim_write_outgoing_msg(ShimUpdate *update, nnStr *msg);
int shim_nn_recv_msg(int sock, void **buf);
int shim_nn_send_msg(int sock, void *buf, size_t len);
bool shim_connect(const char *path);

#endif //SHIM_NANOMSG_H
