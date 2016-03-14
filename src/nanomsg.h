//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_NANOMSG_H
#define SHIM_NANOMSG_H

#include "common.h"
#include <nanomsg/nn.h>
#include <nanomsg/pair.h>

int mux_nn_recv_msg(int sock, void **buf);
int mux_nn_send_msg(int sock, void *buf, size_t len);
bool mux_connect(const char *path);

#endif //SHIM_NANOMSG_H
