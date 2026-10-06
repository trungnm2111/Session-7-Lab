#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"

#define NUM_WORKERS 4
#define WORK_US     2000          /* thoi gian "xu ly" moi request */

static struct shm_region *shm;
static int listen_fd = -1;
static int verbose;

/* ------------------------------------------------------------------ */
/* Lock dung chung giua cac process                                    */
/* ------------------------------------------------------------------ */
static void shm_lock(void)
{
    int r = pthread_mutex_lock(&shm->lock);
    (void)r;
}

static void shm_unlock(void)
{
    pthread_mutex_unlock(&shm->lock);
}

/* ------------------------------------------------------------------ */
/* Worker                                                              */
/* ------------------------------------------------------------------ */
static void log_client(int fd)
{
    char line[128];
    snprintf(line, sizeof line,
        "[worker %d] accepted client fd=%d, reading request header ...",getpid(), fd);
    if (verbose)
        puts(line);
}

static void process_request(int fd)
{
    struct msg_hdr hdr;
    char payload[DATA_MAX];
    struct reply rep;

    read(fd, &hdr, sizeof hdr);             /* doc header */
    uint32_t n = hdr.len < DATA_MAX ? hdr.len : DATA_MAX;
    read(fd, payload, n);                   /* doc payload */

    shm_lock();

    uint64_t h = shm->head;
    struct slot *s = &shm->ring[h % RING_SLOTS];
    s->len = hdr.len;
    s->worker_pid = (uint32_t)getpid();
    memcpy(s->data, payload, n);
    __atomic_store_n(&shm->head, h + 1, __ATOMIC_RELEASE);   /* publish */

    rep.status = 0;
    rep.seq = (uint32_t)++shm->seq;

    usleep(WORK_US);                        /* gia lap xu ly */


    /* Tra reply trong lock de "dam bao thu tu seq" */
    write(fd, &rep, sizeof rep);
    shm_unlock();
}

static void handle_client(int fd)
{
    log_client(fd);
    process_request(fd);
}

static void worker_main(void)
{
    while(1) {
        int c = accept(listen_fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR)    continue;

            perror("accept");
            _exit(1);
        }
        
        handle_client(c);
        close(c);
    }
}

/* ------------------------------------------------------------------ */
/* Collector                                                           */
/* ------------------------------------------------------------------ */
static void consume(const struct slot *s)
{
    char buf[DATA_MAX + 1];

    memcpy(buf, s->data, s->len);
    buf[s->len] = '\0';
    if (verbose)
        printf("[collector] tu worker %u: \"%s\"\n", s->worker_pid, buf);
}

static void collector_main(void)
{
    for (;;) {
        uint64_t head = __atomic_load_n(&shm->head, __ATOMIC_ACQUIRE);
        while (shm->tail < head) {
            struct slot s = shm->ring[shm->tail % RING_SLOTS];
            consume(&s);
            shm->tail++;
        }
        usleep(20000);
    }
}

/* ------------------------------------------------------------------ */
/* Supervisor                                                          */
/* ------------------------------------------------------------------ */
static pid_t spawn(void (*fn)(void))
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        fn();
        _exit(0);
    }
    return pid;
}

static void setup_shm(void)
{
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, sizeof *shm) < 0) {
        perror("shm_open/ftruncate");
        exit(1);
    }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd);
    memset(shm, 0, sizeof *shm);

    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&shm->lock, &a);
    pthread_mutexattr_destroy(&a);
}

static void setup_socket(void)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCK_PATH, sizeof addr.sun_path - 1);
    unlink(SOCK_PATH);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0 || bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(listen_fd, 128) < 0) {
        perror("socket/bind/listen");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    struct rlimit rl;
    if (getrlimit(RLIMIT_CORE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_CORE, &rl);
    }

    setup_shm();
    setup_socket();

    pid_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++)
        workers[i] = spawn(worker_main);
    pid_t collector = spawn(collector_main);

    printf("[ringd] pid=%d, %d workers, collector=%d, socket=%s%s\n",
           getpid(), NUM_WORKERS, collector, SOCK_PATH,"");

    while(1) {
        int st;
        pid_t dead = waitpid(-1, &st, 0);
        if (dead < 0) {
            if (errno == EINTR)    continue;
            
            perror("waitpid");
            break;
        }

        char why[64] = "";
        if (verbose) {
            if (WIFSIGNALED(st)){
                snprintf(why, sizeof why, " (signal %d: %s)", WTERMSIG(st), strsignal(WTERMSIG(st)));
            }
                
            else if (WIFEXITED(st)){
                snprintf(why, sizeof why, " (exit %d)", WEXITSTATUS(st));
            }
        }

        if (dead == collector) {
            sleep(1);
            collector = spawn(collector_main);
            printf("[ringd] collector %d exited%s, respawned as %d\n", dead, why,
                   collector);
            continue;
        }
        for (int i = 0; i < NUM_WORKERS; i++) {
            if (workers[i] == dead) {
                workers[i] = spawn(worker_main);
                printf("[ringd] worker %d exited%s, respawned as %d\n", dead, why,
                       workers[i]);
            }
        }
    }
    return 0;
}

