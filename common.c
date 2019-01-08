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



int unix_domain_server_init(char *path){
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

int unix_domain_client_init(char *path){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); 
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);

    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    return fd; 
}

