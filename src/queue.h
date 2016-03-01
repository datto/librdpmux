//
// Created by sramanujam on 2/4/16.
//

#ifndef SHIM_QUEUE_H
#define SHIM_QUEUE_H

#include "common.h"
#include "lib/libqueue.h"

void *shim_queue_dequeue(ShimMsgQueue *q);
void shim_queue_enqueue(ShimMsgQueue *q, ShimUpdate *update);
void shim_queue_clear(ShimMsgQueue *q);


#endif //SHIM_QUEUE_H
