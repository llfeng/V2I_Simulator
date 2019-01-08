/*
 * =====================================================================================
 *
 *       Filename:  trace.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2019年01月08日 14时52分20秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  lilei.feng , lilei.feng@pku.edu.cn
 *        Company:  Peking University
 *
 * =====================================================================================
 */


#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>  
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include "common.h"

#define TRACE_MAX_LEN   512

char *trace_sock_path = "trace.sock";
char *trace_file_path = "trace.log";



int main(){
    int server_fd = unix_domain_server_init(trace_sock_path);
    int trace_fd = open(trace_file_path, O_RDWR|O_CREAT, 777);
    
    int conn[2];
    conn[0] = accept(server_fd, NULL, NULL);
    conn[1] = accept(server_fd, NULL, NULL);

    int maxfd = conn[0] > conn[1] ? conn[0] : conn[1];

    int recvlen = 0;
    char recvbuf[TRACE_MAX_LEN];
    while(1){
        fd_set fds;
        memset(&fds, 0, sizeof(fd_set));
        FD_SET(conn[0], &fds);
        FD_SET(conn[1], &fds);
        int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);        
        if(ret > 0){
            if(FD_ISSET(conn[0], &fds)){
                recvlen = read(conn[0], recvbuf, TRACE_MAX_LEN);
                write(trace_fd, recvbuf, recvlen);
            }
            if(FD_ISSET(conn[1], &fds)){
                recvlen = read(conn[1], recvbuf, TRACE_MAX_LEN);
                write(trace_fd, recvbuf, recvlen);
            }
        }else{
            printf("socket error!\n");
            return -1;
        }
    }
    close(server_fd);
    close(trace_fd);
    return 0;
}
