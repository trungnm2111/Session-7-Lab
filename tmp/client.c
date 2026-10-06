#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"

/* timeout_ms > 0: gioi han thoi gian cho connect/send/recv */
static int connect_daemon(int timeout_ms)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCK_PATH, sizeof addr.sun_path - 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }
    if (timeout_ms > 0) {
        struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); /* ca connect */
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* 0 = ok, 1 = timeout, -1 = loi */
static int one_request(int id, int timeout_ms, uint32_t *seq_out)
{
    int fd = connect_daemon(timeout_ms);
    if (fd < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 1 : -1;

    char payload[DATA_MAX];
    int n = snprintf(payload, sizeof payload, "sensor=%d temp=%d.%d", getpid() % 100, 20 + id % 10, id % 10);
    struct msg_hdr hdr = { MSG_MAGIC, (uint32_t)n };

    if (write(fd, &hdr, sizeof hdr) != sizeof hdr || write(fd, payload, n) != n) {
        close(fd);
        return -1;
    }

    struct reply rep;
    ssize_t r = read(fd, &rep, sizeof rep);
    close(fd);                       /* timeout -> dong ket noi luon */
    if (r == (ssize_t)sizeof rep) {
        *seq_out = rep.seq;
        return 0;
    }
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 1;
    return -1;
}

static void cmd_send(int count)
{
    for (int i = 0; i < count; i++) {
        uint32_t seq;
        int r = one_request(i, 0, &seq);
        if (r == 0)
            printf("request %d -> seq %u\n", i, seq);
        else
            printf("request %d -> loi\n", i);
    }
}

static void cmd_load(int conc, int count, int timeout_ms)
{
    for (int c = 0; c < conc; c++) {
        if (fork() == 0) {
            int ok = 0, to = 0, err = 0;
            for (int i = 0; i < count; i++) {
                uint32_t seq;
                int r = one_request(i, timeout_ms, &seq);
                if (r == 0) ok++;
                else if (r == 1) to++;
                else err++;
            }
            printf("[client %d] ok=%d timeout=%d error=%d\n", getpid(), ok, to, err);
            _exit(0);
        }
    }
    while (wait(NULL) > 0)
        ;
}

static void cmd_probe(void)
{
    int fd = connect_daemon(0);
    if (fd < 0) {
        perror("connect");
        exit(1);
    }
    close(fd);
    printf("probe: connect + close\n");
}

static void cmd_stat(void)
{
    int fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd < 0) {
        perror("shm_open");
        exit(1);
    }
    struct shm_region *s = mmap(NULL, sizeof *s, PROT_READ, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    printf("head=%lu tail=%lu seq=%lu backlog=%lu\n", (unsigned long)s->head,
           (unsigned long)s->tail, (unsigned long)s->seq,
           (unsigned long)(s->head - s->tail));
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) {
        fprintf(stderr, "usage: %s send [N] | load C N [timeout_ms] | probe | stat\n",
                argv[0]);
        return 1;
    }
    if (!strcmp(argv[1], "send"))
        cmd_send(argc > 2 ? atoi(argv[2]) : 5);
    else if (!strcmp(argv[1], "load") && argc >= 4)
        cmd_load(atoi(argv[2]), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 0);
    else if (!strcmp(argv[1], "probe"))
        cmd_probe();
    else if (!strcmp(argv[1], "stat"))
        cmd_stat();
    else {
        fprintf(stderr, "lenh khong hop le\n");
        return 1;
    }
    return 0;
}