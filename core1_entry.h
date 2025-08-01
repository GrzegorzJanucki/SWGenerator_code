#ifndef CORE1_ENTRY_H
#define CORE1_ENTRY_H

#include "pico/util/queue.h"

#define READY_FLAG 234
#define TARGET_T 101

typedef struct {
    uint8_t msgId;
    uint8_t objId;
    int32_t command;
    void *dataPtr;
    uint16_t dataLen;
} queue_entry_t;

extern queue_t core0_to_core1_queue;
extern queue_t core1_to_core0_queue;

void core1_entry(void);

#endif