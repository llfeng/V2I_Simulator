/*
 * =====================================================================================
 *
 *       Filename:  common.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2019年01月08日 15时44分27秒
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
#include <stdint.h>
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
#include <sys/time.h>
#include <pthread.h>
#include "common.h"


int random_fd = -1;

pthread_mutex_t random_mutex;

void init_random(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
	printf("srand:%ld\n", tv.tv_usec);
    srand(tv.tv_usec);
//    pthread_mutex_init(&random_mutex, 0);
}


uint64_t get_random(){
    return random();
/*
    pthread_mutex_lock(&random_mutex);
    random_fd = open ("/dev/random", O_RDONLY);
    if(random_fd < 0){
        printf("get random error\n");
    }
    uint64_t r;
    int len = read(random_fd, &r, sizeof(r));
    if(len < 0){
        printf("read random error\n");
    }
    close(random_fd);
    pthread_mutex_unlock(&random_mutex);
    return r;
*/    
}

void destroy_random(){
//    close(random_fd);
}

int unix_domain_server_init(const char *path){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); 
    if(fd < 0){ 
        perror("create sock fail:");
    }   
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
    unlink(path);
    if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){ 
        perror("bind fail:");
    }   
    if(listen(fd, 5) < 0){ 
        perror("listen fail:");
    }   
    return fd; 
}

int unix_domain_client_init(const char *path){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); 
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    return fd; 
}



void get_send_pos(reader_request_t *reader_request, double *x, double *y){
    *x = reader_request->posx + (reader_request->start_time - reader_request->init_time) * reader_request->velocity;
    *y = reader_request->posy;
}

void calibrate_reader_pos(reader_request_t *reader_request, int cur_time, double *x, double *y){
    *x = reader_request->posx + (cur_time - reader_request->init_time) * reader_request->velocity;
    *y = reader_request->posy;
}


void get_recv_pos(reader_request_t *reader_request, tag_response_t *tag_response, double *x, double *y){
    *x = reader_request->posx + (tag_response->start_time + (tag_response->plen << 3)*1000/UPLINK_BITRATE + PREAMBLE_TIME - reader_request->init_time) * reader_request->velocity;
    *y = reader_request->posy;
}

