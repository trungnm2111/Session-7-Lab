#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>

#define SERV_PORT 8888
#define BUFFER_SIZE  1024
#define BACKLOG  5
#define MAX_CLIENTS 15

volatile sig_atomic_t got_sigint = 0, got_sigusr1 = 0 ;

typedef struct tcp_connect_t{
    int listen_fd;
    int connect_fd[FD_SETSIZE];
    int number_connect;
    fd_set master_set;
    int max_fd;
} server_conn_t;

static void signal_handler(int signo){
    if (signo == SIGINT){
        got_sigint = 1;
    }else if (signo == SIGUSR1){
        got_sigusr1 = 1;
    }    
}

void send_broadcast(int send_fd, char *buffer, int buf_len, server_conn_t* server_tcp);

void init_signal(void){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

int create_connection(void){
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERV_PORT);

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    return listenfd;
}

int add_new_fd(int fd, server_conn_t* server_tcp){
    if(fd >= FD_SETSIZE )    return -1;
    if(server_tcp->number_connect >= MAX_CLIENTS )    return -1;

    if(server_tcp->connect_fd[fd] != -1){
        return -1;
    }

    server_tcp->connect_fd[fd] = fd;
    server_tcp->number_connect ++;
    return 0;
}

int delete_fd_connect(int fd_conncet_left, server_conn_t* server_tcp){
    if(server_tcp->connect_fd[fd_conncet_left] != -1){
        server_tcp->connect_fd[fd_conncet_left] = -1;
        server_tcp->number_connect --;
        FD_CLR(fd_conncet_left, &server_tcp->master_set);
        close(fd_conncet_left);
        char buf[BUFFER_SIZE];
        snprintf(buf, BUFFER_SIZE ,"Client %d has left\n", fd_conncet_left);
        send_broadcast(server_tcp->listen_fd, buf, strlen(buf), server_tcp);
        return 0;
    }
    return -1;
}

void send_broadcast(int send_fd, char *buffer, int buf_len, server_conn_t* server_tcp){
    for(int fd = 0 ; fd <= server_tcp->max_fd; fd++)
    {
        if (!FD_ISSET(fd, &server_tcp->master_set))  continue;
        if((fd != server_tcp->listen_fd && fd != send_fd) && (server_tcp->connect_fd[fd] != -1) ){
            send(fd, buffer, buf_len,MSG_NOSIGNAL);
        }        
    }

    return;
}

void handler_connect(int connect_fd, server_conn_t* server_tcp){
    char buf[BUFFER_SIZE];
    ssize_t n = read(connect_fd, buf, sizeof(buf)-1);
    if (n > 0) {
        buf[n] = '\0';
        char payload_name[BUFFER_SIZE+32];
        snprintf(payload_name, BUFFER_SIZE+32, "[fd %d]: send:%s",connect_fd, buf);
        printf("Payload %s", payload_name);
        // write(connect_fd, buf, n);          /* echo lai cho client */
        send_broadcast(connect_fd, payload_name, strlen(payload_name), server_tcp);
    } else if (n == 0) {
        delete_fd_connect(connect_fd, server_tcp);
    } else if (errno != EINTR) {
        perror("read");
        delete_fd_connect(connect_fd, server_tcp);
    }
}

void clean_sock(server_conn_t* server_tcp){
    /* Don dep */
    for (int fd = 0; fd <= server_tcp->max_fd; fd++) {
        if (FD_ISSET(fd, &server_tcp->master_set)) {
            close(fd);
        }
    }
    printf("Server da thoat.\n");
}

int main(){
    init_signal();
    server_conn_t server_tcp;
    server_tcp.listen_fd = create_connection();
    server_tcp.number_connect = 0;
    printf("Server PID=%d dang lang nghe cong %d\n", getpid(), SERV_PORT);
    
    memset(&server_tcp.connect_fd, -1, sizeof(server_tcp.connect_fd));
    fd_set read_set;
    FD_ZERO(&server_tcp.master_set);
    FD_SET(server_tcp.listen_fd, &server_tcp.master_set);
    server_tcp.max_fd = server_tcp.listen_fd;

    while (!got_sigint) {
        read_set = server_tcp.master_set;
        int ready = select(server_tcp.max_fd + 1, &read_set, NULL, NULL, NULL);
        if (ready == -1) {
            if (errno == EINTR) {
                printf("select() bi ngat boi signal (EINTR)\n");
                if (got_sigusr1) {
                    printf("  -> Nhan SIGUSR1\n");
                    got_sigusr1 = 0;
                }
                if (got_sigint) {
                    printf("  -> Nhan SIGINT, thoat chuong trinh\n");
                    break;
                }
                continue;
            }

            perror("select");
            break;
        }
        for (int fd = 0; fd <= server_tcp.max_fd && ready > 0; fd++) {
            if (!FD_ISSET(fd, &read_set))   continue;

            ready --;
            if(fd == server_tcp.listen_fd){
                struct sockaddr_in cli;
                socklen_t len = sizeof(cli);
                int cfd = accept(server_tcp.listen_fd, (struct sockaddr *)&cli, &len);
                if (cfd == -1) {
                    if (errno != EINTR)   perror("accept");

                    continue;
                }

                if(add_new_fd(cfd, &server_tcp) == -1){
                    printf("Err: add new sock connect \n");
                    char buf_send[32];
                    snprintf(buf_send, 32 ,"full slot room, Fd %d has left\n", cfd);
                    write(cfd, buf_send, strlen(buf_send));
                    close(cfd);
                    continue;
                }

                printf("Client moi: %s:%d (fd=%d)\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port), cfd);
                
                FD_SET(cfd, &server_tcp.master_set);
                if (cfd > server_tcp.max_fd)   server_tcp.max_fd = cfd;
            }else{
                handler_connect(fd , &server_tcp);
            }
        }
    }
    clean_sock(&server_tcp);
    return 0;
}