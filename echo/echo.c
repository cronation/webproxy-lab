#include "csapp.h"
#include <stdio.h>

#define FILE_NAME "myfile.text"

#define DEF_PERM (S_IRUSR | S_IWUSR | S_IXUSR)

int parent_func() {
    int result;

    char *hostname = "127.0.0.1";
    char *port = "7777";

    int listen_fd = Open_listenfd(port);
    int connect_fd;

    // client 정보 저장용
    socklen_t clientlen;
    struct sockaddr clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    printf("P: socket %d is awaiting for client connection...\n", listen_fd);
    connect_fd = Accept(listen_fd, &clientaddr, &clientlen);
    if (connect_fd == -1) {
        printf("P: accept() failed\n");
        return -1;
    }
    // client 정보 해석
    result = getnameinfo(&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    if (result == -1) {
        printf("P: getnameinfo() failed\n");
        Close(listen_fd);
        Close(connect_fd);
        return -1;
    }
    printf("P: connection established with %s:%s, socket %d\n", client_hostname, client_port, connect_fd);

    // receive messages and echo
    char buf[MAXLINE];

    while (read(connect_fd, buf, MAXLINE) != 0) {
        printf("P: received message: %s", buf);
        printf("P: sending back\n");
        write(connect_fd, buf, MAXLINE);
    }

    printf("P: closing sockets\n");
    result = close(listen_fd);
    if (result == -1) {
        printf("P: close() failed\n");
        return -1;
    }
    result = close(connect_fd);
    if (result == -1) {
        printf("P: close() failed\n");
        return -1;
    }

    return 0;
}

int child_func() {
    int result;

    char *hostname = "127.0.0.1";
    char *port = "7777";

    int fd = Open_clientfd(hostname, port);

    char buf[MAXLINE];

    // send and receive messages
    while (fgets(buf, MAXLINE, stdin) != NULL) {
        write(fd, buf, MAXLINE);
        read(fd, buf, MAXLINE);
        printf("C: received message: %s", buf);
    }

    printf("C: closing socket\n");
    result = close(fd);
    if (result == -1) {
        printf("C: close() failed\n");
        return -1;
    }
    Close(fd);

    return 0;
}


int main() {

    pid_t pid = fork();

    if (pid == 0) {
        printf("I am child\n");
        child_func();
        return 0;
    } else {
        printf("I am parent\n");
        parent_func();

        waitpid(pid, NULL, 0);
        printf("reaped all; exiting\n");
        return 0;
    }

    return 0;
}