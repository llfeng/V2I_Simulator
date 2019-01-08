/*
 * =====================================================================================
 *
 *       Filename:  simulator.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2018年12月31日 16时16分29秒
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
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "common.h"

#define FRAME_MAX_LEN   32
#define PAYLOAD_MAX_LEN 24

#define READER_MAX_NUM  10
#define TAG_MAX_NUM  10

#define ACK     1
#define NACK    2
#define CACK    3

#define DISCOVERY_REQUEST           0
#define DISCOVERY_REQUEST_ACK       1
#define DISCOVERY_REQUEST_NACK      2
#define DISCOVERY_REQUEST_CACK      3
#define QUERY_REQUEST       4
#define QUERY_REQUEST_ACK   5
#define QUERY_REQUEST_NACK  6
#define QUERY_REQUEST_CACK  7

typedef struct{
    char reader;
    char tag;
}addr_pair_t;

typedef struct{
    char src;
    char ack;
    char type;
    char dst;
    char collision_num;
    char plen;
    char addr_pair_num;
    addr_pair_t addr_pair[TAG_MAX_NUM];
    char payload[PAYLOAD_MAX_LEN];
    char fcs;
}downlink_frame_t;

typedef struct{
    int conn;
    int rxbuflen;
    char rxbuf[FRAME_MAX_LEN];
    downlink_frame_t downlink_frame;
}reader_t;

typedef struct{
    char dst;
    char src;
    char plen;
    char payload[PAYLOAD_MAX_LEN];
    char fcs;
}uplink_frame_t;

typedef struct{
    char reader;
    char tag;
    int vaild;
}tag_alias_t;

typedef struct{
    int conn;
    char dst;
    char addr;
//    uplink_frame_t uplink_frame;
    char silent[READER_MAX_NUM];
    char silent_num;
    char txbuflen;
    char txbuf[FRAME_MAX_LEN];
    char alias_num;
    tag_alias_t alias[READER_MAX_NUM];
}tag_t;

typedef struct{
    int type;
    int plen;
    char payload[FRAME_MAX_LEN];
}proxy_msg_t;

typedef struct{
    int vaild;
    int conn;
    int plen;
    char payload[FRAME_MAX_LEN];
}tag_info_t;


pthread_mutex_t tag_mutex;

tag_t tag_item_table[TAG_MAX_NUM];

char *trace_sock_path = "trace.sock";
char *tag_proxy_path = "tag_proxy.sock";
char *simulator_server_path = "server.socket";

int reader_num;
int tag_num;

int trace_fd = -1;


void update_alias(tag_t *tag, char short_addr, char reader_addr){
    int exist_flag = 0;
    for(int i = 0; i < tag->alias_num; i++){
        if(reader_addr == tag->alias[i].reader){
            tag->alias[i].tag = short_addr;
            exist_flag = 1;
            break;
        }
    }
    if(exist_flag == 0){
        tag->alias[tag->alias_num].reader = reader_addr;
        tag->alias[tag->alias_num].tag = short_addr;
        tag->alias_num++;
    }
}

void delete_alias(tag_t *tag, char reader_addr){
    int exist_flag = 0;
    for(int i = 0; i < tag->alias_num; i++){
        if(reader_addr == tag->alias[i].reader){
            exist_flag = 1;
        }
        if(exist_flag){
            tag->alias[i] = tag->alias[i+1];
        }
    }
    if(exist_flag){
        tag->alias_num--;
    }
}

int lookup_alias(tag_t *tag, char short_addr, char reader_addr, int acked){
    for(int i = 0; i < tag->alias_num; i++){
        if(reader_addr == tag->alias[i].reader && 
        short_addr == tag->alias[i].tag){
            if(acked){
                if(tag->alias[i].vaild){
                    return 1;
                }
            }else{
                return 1;
            }
        }
    }
    return 0;
}

void send_to_proxy(tag_t *tag){
    tag->txbuf[0] = tag->dst;
    tag->txbuf[1] = (tag->addr << 4);
    tag->txbuf[1] += 0;
    tag->txbuflen = 2;
    printf("[%ld----send: %02x %02x---]\n", time(NULL), tag->txbuf[0], tag->txbuf[1]);
    int sent_bytes = write(tag->conn, tag->txbuf, tag->txbuflen);
}


void keep_silent(tag_t *tag, int reader_addr){
    int exist_flag = 0;
    for(int i = 0; i < tag->silent_num; i++){
        if(tag->silent[i] == reader_addr){
            exist_flag = 1;
            break;
        }
    }
    
    if(exist_flag == 0){
        tag->silent[tag->silent_num++] = reader_addr;
    }
}

int is_silent(tag_t *tag, int reader_addr){
    for(int i = 0; i < tag->silent_num; i++){
        if(tag->silent[i] == reader_addr){
            return 1;
        }
    }
    return 0;
}

void parse_downlink(tag_t *tag, char *buf){     //mac
    char reader_addr = buf[0];
    char frame_type = buf[1] >> 4;
    char frame_state = frame_type & 0x04;
    char tag_addr = buf[1] & 0x0F;

    pthread_mutex_lock(&tag_mutex);     
    printf("[before]:\n");
    for(int i = 0; i < tag->alias_num; i++){
        printf("tag->alias[%d]--reader:%d,tag:%d,vaild:%d\n", i, tag->alias[i].reader, tag->alias[i].tag,tag->alias[i].vaild);
    }
    for(int i = 0; i < tag->silent_num; i++){
        printf("tag->silent[%d]:%d\n", i, tag->silent[i]);
    }
    switch(frame_type & 0x03){
        case ACK:
            for(int i = 0; i < tag->alias_num; i++){
                if(tag->alias[i].reader == reader_addr && tag->alias[i].tag == tag_addr - 1){ 
                    tag->alias[i].vaild = 1;
                    keep_silent(tag, reader_addr);
                }
            }
            break;
        case NACK:
            break;
        case CACK:
            break;
        default:
            break;
    }

    if(frame_state == DISCOVERY_REQUEST){
        char plen = buf[2] & 0x0F;
        printf("[%ld--recv:%02x %02x %02x ", time(NULL), buf[0], buf[1], buf[2]);
        for(int i = 0; i < plen; i += 2){
            printf("%02x %02x ", buf[i+3], buf[i+4]);
            if(lookup_alias(tag, buf[i+4], buf[i+3], 1) == 1){
                keep_silent(tag, reader_addr);
            }
        }
        printf("---]\n");
        if(is_silent(tag, reader_addr) == 1){
            delete_alias(tag, reader_addr);
        }else{
            char addr_range = 0;
            char collision_num = buf[2] >> 4;
            if(collision_num == 0){
                addr_range = 1;
            }else{
                addr_range = 2 * collision_num;
            }
            char short_addr = random()%addr_range;            
            update_alias(tag, short_addr, reader_addr);
            if(short_addr == 0){
                tag->addr = short_addr;
                tag->dst = reader_addr;
                send_to_proxy(tag);
            }
        }
    }else{  //QUERY_REQUEST
        printf("[%ld--recv:%02x %02x]\n", time(NULL), buf[0], buf[1]);
        if(lookup_alias(tag, tag_addr, reader_addr, 0) == 1){
            tag->addr = tag_addr;
            tag->dst = reader_addr;
            send_to_proxy(tag);
        }
    }
    printf("[after]:\n");
    for(int i = 0; i < tag->alias_num; i++){
        printf("tag->alias[%d]--reader:%d,tag:%d,vaild:%d\n", i, tag->alias[i].reader, tag->alias[i].tag,tag->alias[i].vaild);
    }
    for(int i = 0; i < tag->silent_num; i++){
        printf("tag->silent[%d]:%d\n", i, tag->silent[i]);
    }
    pthread_mutex_unlock(&tag_mutex);
}

tag_t *create_tag(){
    tag_t *tag = (tag_t *)malloc(sizeof(tag_t));
    memset(tag, 0, sizeof(tag_t));
    return tag;
}

void tag_init(tag_t *tag, int conn){
    tag->alias_num = 0;
    memset(tag->alias, 0, sizeof(tag->alias));

    tag->silent_num = 0; 
    memset(tag->silent, 0, sizeof(tag->silent));

    tag->txbuflen = 0;
    memset(tag->txbuf, 0, sizeof(tag->txbuf));

    tag->conn = conn;
}

void *tag_thread(){
	tag_t *tag = create_tag();
    int conn = unix_domain_client_init(tag_proxy_path);
    tag_init(tag, conn);
    while(1){
        char buf[FRAME_MAX_LEN];
        memset(buf, 0, FRAME_MAX_LEN);
        read(conn, buf, FRAME_MAX_LEN);
        parse_downlink(tag, buf);
    }

}


void send_to_tag(tag_info_t *tag_info, char *buf, int buflen){
    for(int i = 0; i < tag_num; i++){
        write(tag_info[i].conn, buf, buflen);
    }
}

//proxy msg type
#define UPLINK_COLLISION    1
#define UPLINK_IDLE         2
#define UPLINK_DATA         3


void send_collision_to_reader(int remote_conn){
    proxy_msg_t msg;
    memset(&msg, 0, sizeof(proxy_msg_t));
    msg.type = UPLINK_COLLISION;
    write(remote_conn, (char *)&msg, sizeof(proxy_msg_t));
}

void send_idle_to_reader(int remote_conn){
    proxy_msg_t msg;
    memset(&msg, 0, sizeof(proxy_msg_t));
    msg.type = UPLINK_IDLE;
    write(remote_conn, (char *)&msg, sizeof(proxy_msg_t));
}

void send_data_to_reader(tag_info_t tag_info, int remote_conn){
    proxy_msg_t msg;
    memset(&msg, 0, sizeof(proxy_msg_t));
    msg.type = UPLINK_DATA;
    msg.plen = tag_info.plen;
    memcpy(msg.payload, tag_info.payload, tag_info.plen);
    write(remote_conn, (char *)&msg, sizeof(proxy_msg_t));
}

void tag_info_handler(tag_info_t *tag_info, int remote_conn){
    char addr_slot[32];
    memset(addr_slot, 0, sizeof(addr_slot));
    int vaild_num = 0;
    int vaild_index = -1;
    char short_addr = 0;
    int collision_flag = 0;
    for(int i = 0; i < tag_num; i++){
        if(tag_info[i].vaild){
            printf("[--will send: %02x %02x--]\n", tag_info[i].payload[0], tag_info[i].payload[1]);
            vaild_index = i;
            vaild_num++;
            short_addr = (tag_info[i].payload[1] >> 4);
            addr_slot[short_addr]++;
        }
        if(addr_slot[short_addr] > 1){
            collision_flag = 1;
            break;
        }
    }

    printf("vaild_num:%d\n", vaild_num);

    if(vaild_num > 1){
        if(collision_flag){
            send_collision_to_reader(remote_conn);
        }else{
            printf("tag vaild num > 1!!!\n");
        }
    }else if(vaild_num == 0){
        send_idle_to_reader(remote_conn);
    }else{      //vaild_num = 1
        send_data_to_reader(tag_info[vaild_index], remote_conn);
    }
    for(int i = 0; i < tag_num; i++){
        tag_info[i].vaild = 0;
        tag_info[i].plen = 0;
        memset(tag_info[i].payload, 0, sizeof(tag_info[i].payload));
    }
}

void *tag_proxy(){
    tag_info_t tag_info[TAG_MAX_NUM];
    memset(tag_info, 0, sizeof(tag_info));
    int local_serverfd = unix_domain_server_init(tag_proxy_path);
    int remote_serverfd = unix_domain_server_init(simulator_server_path);

    for(int i = 0; i < tag_num; i++){
        tag_info[i].conn = accept(local_serverfd, NULL, NULL);
        tag_info[i].vaild = 0;
        tag_info[i].plen = 0;
        memset(tag_info[i].payload, 0, sizeof(tag_info[i].payload));
    }

    int remote_conn = accept(remote_serverfd, NULL, NULL);

    printf("tag_proxy init ok\n");

    int request_flag = 0;
    while(1){
        fd_set fds;
        memset(&fds, 0, sizeof(fd_set));
        FD_SET(remote_conn, &fds);
        int maxfd = remote_conn;
        for(int i = 0; i < tag_num; i++){
            FD_SET(tag_info[i].conn, &fds);
            maxfd = maxfd > tag_info[i].conn ? maxfd : tag_info[i].conn;
        }

        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if(ret > 0){
            if(FD_ISSET(remote_conn, &fds)){
                printf("recv from remote\n");
                char buf[32];
                memset(buf, 0, sizeof(buf));
                int buflen = read(remote_conn, buf, FRAME_MAX_LEN);
                send_to_tag(tag_info, buf, buflen);
                request_flag = 1;
            }
            for(int j = 0; j < tag_num; j++){
                if(FD_ISSET(tag_info[j].conn, &fds)){
                    tag_info[j].plen = read(tag_info[j].conn, tag_info[j].payload, FRAME_MAX_LEN);
                    tag_info[j].vaild = 1;
                }
            }
        }else{
            if(request_flag){
                request_flag = 0;
                tag_info_handler(tag_info, remote_conn);
            }
        }
    }
}




void usage(char *prog){
    printf("Usage:%s <reader_num> <tag_num>\n", prog);
}

int main(int argc, char *argv[]){
    if(argc == 3){
        reader_num = atoi(argv[1]);
        tag_num = atoi(argv[2]);
    }else{
        usage(argv[0]);
        return 0;
    }
    srand(time(NULL));
    pthread_mutex_init(&tag_mutex, 0);

    trace_fd = unix_domain_client_init(trace_sock_path);

    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, tag_proxy, NULL);

    sleep(1);
    pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*tag_num);
    for(int i = 0; i < tag_num; i++){
        pthread_create(&thread_tab[i], NULL, tag_thread, NULL);
    }   
    
    printf("init ok\n");
    while(1);
}

