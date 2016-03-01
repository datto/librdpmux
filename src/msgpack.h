//
// Created by sramanujam on 2/3/16.
//

#ifndef SHIM_MSGPACK_H
#define SHIM_MSGPACK_H

#include <malloc.h>
#include <nanomsg/nn.h>

#include "common.h"
#include "lib/c-msgpack.h"

typedef struct nn_str {
    void *buf; // pointer to char data
    size_t size; // size of data buffer in bytes
    int pos; // current read position in buffer
} nnStr;

size_t shim_write_outgoing_msg(ShimUpdate *update, nnStr *msg);
void shim_process_incoming_msg(void *buf, int nbytes);

#endif //SHIM_MSGPACK_H
