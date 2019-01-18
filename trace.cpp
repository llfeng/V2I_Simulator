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
#include <string.h>
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

const char *trace_sock_path = "trace.sock";



int main(int argc, char *argv[]){
    if(argc != 4) return -1;

    
    
    int reader_num = atoi(argv[1]);
    int tag_num = atoi(argv[2]);
    int tar_round = atoi(argv[3]);
    int round = 0;

    time_t t = time(0);                                                                              
    struct tm ttt = *localtime(&t);  

    char result_file_path[128];
    char trace_file_path[128];

    int server_fd = unix_domain_server_init(trace_sock_path);

    int conn[2];
    conn[0] = accept(server_fd, NULL, NULL);
    conn[1] = accept(server_fd, NULL, NULL);
    int maxfd = conn[0] > conn[1] ? conn[0] : conn[1];
        
    int recvlen = 0;
    char recvbuf[TRACE_MAX_LEN];

    
    sprintf(result_file_path, "result-%4d-%02d-%02d_%02d_%02d_%02d_%dreader_%dtag.csv", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour, ttt.tm_min, ttt.tm_sec, reader_num, tag_num);
    sprintf(trace_file_path, "trace-%4d-%02d-%02d_%02d_%02d_%02d_%dreader_%dtag.log", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour, ttt.tm_min, ttt.tm_sec, reader_num, tag_num);
    int res_fd = open(result_file_path, O_RDWR|O_CREAT, 0664);
    int trace_fd = open(trace_file_path, O_RDWR|O_CREAT, 0664);
    while(1){
        fd_set fds;
        memset(&fds, 0, sizeof(fd_set));
        FD_SET(conn[0], &fds);
        FD_SET(conn[1], &fds);
        int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);        
        if(ret > 0){
            if(FD_ISSET(conn[0], &fds)){
                memset(recvbuf, 0, sizeof(recvbuf));
                recvlen = read(conn[0], recvbuf, TRACE_MAX_LEN);
                write(trace_fd, recvbuf, recvlen);
                if(recvlen >= 6 && (memcmp(recvbuf, "FINISH", 6) == 0)){
                    round++;
                    printf("round:%d\n", round);
                    if(round < tar_round){
                        write(conn[1], "RESET", 5);
                        write(conn[0], "RESET", 5);
                    }else{
                        write(conn[1], "KILL", 4);
                        write(conn[0], "KILL", 4);
                        break;
                    }
                }
                char *res_str = NULL;
                if(res_str = strstr(recvbuf, "elapsed_time:")){
                    char *end_str = strstr(res_str, "\n");
                    char tmp[64];
                    snprintf(tmp, end_str-res_str, "%s, ", res_str);
                }
            }
            if(FD_ISSET(conn[1], &fds)){
                memset(recvbuf, 0, sizeof(recvbuf));
                recvlen = read(conn[1], recvbuf, TRACE_MAX_LEN);
                write(trace_fd, recvbuf, recvlen);
                if(recvlen >= 6 && (memcmp(recvbuf, "FINISH", 6) == 0)){
                    round++;
                    printf("round:%d\n", round);
                    if(round < tar_round){
                        write(conn[1], "RESET", 5);
                        write(conn[0], "RESET", 5);
                    }else{
                        write(conn[1], "KILL", 4);
                        write(conn[0], "KILL", 4);
                        break;
                    }
                }
                char *res_str = NULL;
                if(res_str = strstr(recvbuf, "elapsed_time:")){
                    write(res_fd, res_str, strlen(res_str));
                }
            }
        }else{
            printf("socket error!\n");
            return -1;
        }
    }
    close(trace_fd);

    close(server_fd);
    return 0;
}
