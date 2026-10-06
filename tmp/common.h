#ifndef RINGD_COMMON_H
#define RINGD_COMMON_H

#include <stdint.h>
#include <pthread.h>

#define SOCK_PATH   "/tmp/ringd.sock"
#define SHM_NAME    "/ringd_shm"
#define RING_SLOTS  64
#define DATA_MAX    64
#define MSG_MAGIC   0x52494E47u          /* "RING" */

/* Client -> worker: header roi toi payload (len byte) */
struct msg_hdr {
    uint32_t magic;
    uint32_t len;
};

/* Worker -> client. Chu y: co 3 byte padding sau 'status'. */
struct reply {
    uint8_t  status;
    uint32_t seq;
};

struct slot {
    uint32_t len;
    uint32_t worker_pid;
    char     data[DATA_MAX];
};

struct shm_region {
    pthread_mutex_t lock;     /* chi cac worker (writer) dung lock nay */
    uint64_t head;            /* slot tiep theo se duoc ghi (da publish) */
    uint64_t tail;            /* slot tiep theo collector se doc */
    uint64_t seq;             /* so request da xu ly */
    struct slot ring[RING_SLOTS];
};

#endif