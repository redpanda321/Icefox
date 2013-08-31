/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _CPR_DARWIN_IPC_H_
#define _CPR_DARWIN_IPC_H_

#include "cpr_threads.h"
#include <pthread.h>

/* Enable support for cprSetMessageQueueThread API */
#define CPR_USE_SET_MESSAGE_QUEUE_THREAD

/* Maximum message size allowed by CNU */
#define CPR_MAX_MSG_SIZE  8192

/* Our CNU msgtype */
#define PHONE_IPC_MSG 1


/* For gathering statistics regarding message queues */
typedef struct {
    char name[16];
    uint16_t maxCount;
    uint16_t currentCount;
    uint32_t totalCount;
    uint32_t rcvTimeouts;
    uint32_t sendErrors;
    uint32_t reTries;
    uint32_t highAttempts;
    uint32_t selfQErrors;
    uint16_t extendedDepth;
} cprMsgQueueStats_t;

/*
 * Mutex for updating the message queue list
 */
extern pthread_mutex_t msgQueueListMutex;

/**
 * cprShowMessageQueueStats
 *
 * Display statistics for all message queues
 *
 * Comments: Function only applicable to CNU
 */
int32_t
cprShowMessageQueueStats(int32_t argc, const char *argv[]);

/**
 * cprGetDepth
 *
 * Get depth of a message queue
 */
uint16_t cprGetDepth(cprMsgQueue_t msgQueue);


#endif
