/*
 * =====================================================================================
 *
 *       Filename:  reader.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2018年12月31日 16时12分48秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  lilei.feng , lilei.feng@pku.edu.cn
 *        Company:  Peking University
 *
 * =====================================================================================
 */

//downlink bitrate
//uplink bitrate
//downlink backoff window

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

#define USE_ROUND_ADDR  1


#define ENERGY_CHECK_TIME   24

#define DEFAULT_UPLINK_LEN  4
#define DEFAULT_DOWNLINK_LEN   8

#define UPLINK_ACK_TIME 24  //preamble


#define DOWNLINK_WINDOW 8
#define DOWNLINK_BITRATE 10000

#define DOWN_SLOT_TIME   ((DEFAULT_DOWNLINK_LEN << 3)*1000/DOWNLINK_BITRATE)

#define UPLINK_BITRATE  256

#define FRAME_MAX_LEN   32
#define PAYLOAD_MAX_LEN 24

#define READER_MAX_NUM  10
#define TAG_MAX_NUM  10

#define ACK     1
#define NACK    2
#define CACK    3


#define DISCOVERY_REQUEST   0
#define DISCOVERY_REQUEST_ACK   1
#define DISCOVERY_REQUEST_NACK   2
#define DISCOVERY_REQUEST_CACK   3
#define QUERY_REQUEST   4
#define QUERY_REQUEST_ACK   5
#define QUERY_REQUEST_NACK  6
#define QUERY_REQUEST_CACK  7

typedef struct{
    char reader;
    char tag;
    char round;
}addr_pair_t;

typedef struct{
    char src;
    char ack;
    char type;
    char dst;
    char collision_num;
    char plen;
    addr_pair_t addr_pair[TAG_MAX_NUM];
    char addr_pair_num;
    char payload[PAYLOAD_MAX_LEN];
    char fcs;
}downlink_frame_t;

typedef struct{
    char dst;
    char src;
    char plen;
    char payload[PAYLOAD_MAX_LEN];
    char fcs;
}uplink_frame_t;

typedef struct{
    char conn;
    char addr;
    char state;
    char ack;
    char query_addr;
    char round;
    char tag_addr_range;
    char collision_num;
    char plen;
    char payload[PAYLOAD_MAX_LEN];
    char pair_num;
    addr_pair_t pair[TAG_MAX_NUM];
    downlink_frame_t downlink_frame;
    char txbuflen;
    char txbuf[FRAME_MAX_LEN];
    int start_time;
    int end_time;
    char last_query_addr;
    int recv_tag_num;
    char test_tab[TAG_MAX_NUM];
    uint64_t elapsed_time;    
}reader_t;

typedef struct{
    int type;
    int reader_frame_state;
    long int start_time;
    int backoff_time;
    int blocked_time;
    int addr;
    int response_reader;
    int buflen;
    char buf[32];
}task_t;


typedef struct{
    int conn;
    int vaild;
    uint64_t start_time;
    uint64_t end_time;
    int backoff_time;
    int addr;
    int plen;
    char payload[32];
}reader_info_t;

typedef struct{
    int type;
    int plen;
    char payload[32];
}proxy_msg_t;



//Task define 
#define ACTIVE              0
#define UPLINK_COLLISION    1
#define UPLINK_IDLE         2
#define UPLINK_DATA         3
#define DOWNLINK_COLLISION  4
#define CARRIER_BLOCK       5
#define TRIGGER             6
#define RESET               7
#define KILL                8

#define TRACE_TYPE_NUM      9
#define NAME_MAX_LEN        48

char trace_type[TRACE_TYPE_NUM][NAME_MAX_LEN] = {
    "ACTIVE",
    "UPLINK_COLLISION",
    "UPLINK_IDLE",
    "UPLINK_DATA",
    "DOWNLINK_COLLISION",
    "CARRIER_BLOCK",
    "TRIGGER",
    "RESET",
    "KILL"
};


pthread_mutex_t reader_mutex;

pthread_mutex_t reader_finish_mutex;

int g_reader_finish_num = 0;

uint64_t g_elapsed_time = 0;

int reader_num;
int tag_num;

reader_t reader_item_table[READER_MAX_NUM];

int trace_fd = -1;

char trace_buf[512] = {0};

char *trace_sock_path = "trace.sock";
char *reader_proxy_path = "reader_proxy.sock";
char *simulator_server_path = "server.socket";


int gen_start_time(){
    int slot_time = DOWN_SLOT_TIME;
    int offset = get_random()%DOWN_SLOT_TIME;
    return offset + (get_random() % DOWNLINK_WINDOW) * DOWN_SLOT_TIME;
}


reader_info_t *select_reader(reader_info_t *reader_item_table){
    reader_info_t *reader_array[READER_MAX_NUM] = {NULL};

    int active_reader_num = 0;
    for(int i = 0; i < reader_num; i++){
        if(reader_item_table[i].vaild){
            reader_array[active_reader_num++] = &reader_item_table[i];
        }
    }

    reader_info_t *min = NULL;
    for(int i = 0; i < active_reader_num; i++){
        min = reader_array[i];
        for(int j = i+1; j < active_reader_num; j++){            
            if(reader_array[j]->start_time < min->start_time){
                reader_array[i] = reader_array[j];        
                reader_array[j] = min;
                min = reader_array[i];
            }
        }
    }
    
    int side_count = 0;

    if(active_reader_num == 1){
        return reader_array[0];
    }else if(active_reader_num == 0){
        return NULL;
    }

    for(int i = 1; i < active_reader_num; i++){    
        if(i == 1){
            if(reader_array[0]->end_time < reader_array[i]->start_time){
                return reader_array[0];
            }
        }

        if(reader_array[i-1]->end_time < reader_array[i]->start_time){
            side_count++;
        }else{
            side_count = 0;
        }
        if(side_count == 2){
            return reader_array[i-1];
        }

        if(i == active_reader_num){
            if(reader_array[i-1]->end_time < reader_array[i]->start_time){
                return reader_array[i];
            }
        }
    }
    return NULL;
}

void usage(char *prog){
    printf("Usage:%s <reader_num> <tag_num>\n", prog);
}

reader_t *create_reader(){
    reader_t *reader = (reader_t *)malloc(sizeof(reader_t));
    memset(reader, 0, sizeof(reader_t));
    return reader;
}

void destroy_reader(reader_t *reader){
    free(reader);
}


void downlink_collision_handler(reader_t *reader){
//do nothing, just wait for retransmission
}

void carrier_block_handler(reader_t *reader){
//do nothing.
}

void uplink_collision_handler(reader_t *reader, task_t *task){
//    printf("task->response_reader:%d, reader->addr:%d\n", task->response_reader, reader->addr);
    if(task->response_reader == reader->addr){ //for me
        reader->collision_num++;
        reader->query_addr++;
        if(reader->query_addr == reader->tag_addr_range){
            reader->state = DISCOVERY_REQUEST;            
            reader->ack = CACK;
            reader->query_addr = reader->tag_addr_range-1;            
            if(reader->collision_num){
                reader->tag_addr_range = 2*reader->collision_num;
            }else{
                reader->tag_addr_range = 1;
            }
        }else{
            reader->state = QUERY_REQUEST;
            reader->ack = CACK;            
        }
    }else{
        //not for me, do nothing
    }
}

int lookup_pair(reader_t *reader, char reader_addr, char tag_addr){
    for(int i = 0; i < reader->pair_num; i++){
        if(reader->pair[i].reader == reader_addr && reader->pair[i].tag == tag_addr){
            return 1;
        }
    }
    return 0;
}

#if USE_ROUND_ADDR
void data_handler(reader_t *reader, task_t *task){
    if(task->response_reader == reader->addr){      //response data is for me
        reader->elapsed_time += task->buflen * 8 * 1000 / UPLINK_BITRATE;
        reader->query_addr++;
        if(reader->query_addr >= reader->tag_addr_range){
            reader->state = DISCOVERY_REQUEST;
            reader->ack = ACK;
            reader->query_addr = reader->tag_addr_range;
            if(reader->collision_num){
                reader->tag_addr_range = 2*reader->collision_num;
            }else{
                reader->tag_addr_range = 1;
            }
        }else{  
            reader->state = QUERY_REQUEST;
            reader->ack = ACK;            
        }
    }else{  //response data is not for me. Save response data as an address pair.
        if(lookup_pair(reader, task->buf[0], task->buf[1] >> 4) == 0){
            reader->pair[reader->pair_num].reader = task->buf[0];
            reader->pair[reader->pair_num].tag = (task->buf[1] >> 4);
            reader->pair[reader->pair_num].round = (task->buf[2]);
            reader->pair_num++;
        }
    }
}
#else
void data_handler(reader_t *reader, task_t *task){
    if(task->response_reader == reader->addr){      //response data is for me
        reader->elapsed_time += task->buflen * 8 * 1000 / UPLINK_BITRATE;
        reader->query_addr++;
        if(reader->query_addr >= reader->tag_addr_range){
            reader->state = DISCOVERY_REQUEST;
            reader->ack = ACK;
            reader->query_addr = reader->tag_addr_range;
            if(reader->collision_num){
                reader->tag_addr_range = 2*reader->collision_num;
            }else{
                reader->tag_addr_range = 1;
            }
        }else{  
            reader->state = QUERY_REQUEST;
            reader->ack = ACK;            
        }
    }else{  //response data is not for me. Save response data as an address pair.
        if(lookup_pair(reader, task->buf[0], task->buf[1] >> 4) == 0){
            reader->pair[reader->pair_num].reader = task->buf[0];
            reader->pair[reader->pair_num].tag = (task->buf[1] >> 4);
            reader->pair_num++;
        }
    }
}
#endif

void uplink_idle_handler(reader_t *reader, task_t *task){
    if(task->response_reader == reader->addr){
        reader->query_addr++;
//        printf("---tag_range:%d\n", reader->tag_addr_range);
        if(reader->query_addr == reader->tag_addr_range){
            reader->state = DISCOVERY_REQUEST;
            reader->ack = NACK;
            reader->query_addr = reader->tag_addr_range-1;            
            if(reader->collision_num){
                reader->tag_addr_range = 2*reader->collision_num;
            }else{
                reader->tag_addr_range = 1;
            }
        }else{
            reader->state = QUERY_REQUEST;
            reader->ack = NACK;            
        }

    }
}

//如果有连续两个idle slot,就有可能发生downlink冲突(QUERY和DISCOVERY)


void reader_init(reader_t *reader, int conn){
    reader->conn = conn;
    reader->addr = conn;
    reader->state = DISCOVERY_REQUEST;
    reader->ack = NACK;
    reader->query_addr = 0;
    reader->tag_addr_range = 1;
    reader->collision_num = 0;
    reader->pair_num = 0;
    reader->round = 0;
    memset(reader->pair, 0, sizeof(reader->pair));
    reader->plen = 0;
    memset(reader->payload, 0, sizeof(reader->payload));
}

int enframe(reader_t *reader, char *txbuf){
#if USE_ROUND_ADDR
    int txbuflen = 0;
    txbuf[0] = reader->addr;
    txbuf[1] = ((reader->state + reader->ack) << 4);
    txbuf[1] += reader->query_addr;
    if(reader->state == DISCOVERY_REQUEST){
        reader->round++;
        txbuf[2] = reader->round;
        txbuf[3] = (reader->collision_num << 4);
        txbuf[3] += reader->pair_num * 3;        
        for(int i = 0; i < reader->pair_num; i++){
            txbuf[i*2+4] = reader->pair[i].reader;
            txbuf[i*2+5] = reader->pair[i].tag;
            txbuf[i*2+6] = reader->pair[i].round;
            reader->pair[i].reader = 0;
            reader->pair[i].tag = 0;
            reader->pair[i].round = 0;
        }
        txbuflen = 4 + reader->pair_num * 3;
        txbuf[txbuflen++] = 0;
        reader->pair_num = 0;
        reader->collision_num = 0;
    }else{
        txbuf[2] = reader->round;
        txbuf[3] = 0;
        txbuflen = 4;
    }
    return txbuflen;
     
#else
    int txbuflen = 0;
    txbuf[0] = reader->addr;
    txbuf[1] = ((reader->state + reader->ack) << 4);
    txbuf[1] += reader->query_addr;
    if(reader->state == DISCOVERY_REQUEST){
        txbuf[2] = (reader->collision_num << 4);
        txbuf[2] += reader->pair_num * 2;        
        for(int i = 0; i < reader->pair_num; i++){
            txbuf[i*2+3] = reader->pair[i].reader;
            txbuf[i*2+4] = reader->pair[i].tag;
            reader->pair[i].reader = 0;
            reader->pair[i].tag = 0;
        }
        txbuflen = 3 + reader->pair_num * 2;
        txbuf[txbuflen++] = 0;
        reader->pair_num = 0;
        reader->collision_num = 0;
    }else{
        txbuf[2] = 0;
        txbuflen = 3;
    }
    return txbuflen;
#endif    
}



void update_test_tab(reader_t *reader, char tag_uuid){
    int exist_flag = 0;
    for(int i = 0; i < reader->recv_tag_num; i++){
        if(reader->test_tab[i] == tag_uuid){
            exist_flag = 1;
            break;
        }
    }
    if(exist_flag == 0){
        reader->test_tab[reader->recv_tag_num++] = tag_uuid;
    }
    if(reader->recv_tag_num == tag_num){
        pthread_mutex_lock(&reader_finish_mutex);
        g_reader_finish_num++;
        if(g_elapsed_time < reader->elapsed_time){
            g_elapsed_time = reader->elapsed_time;
        }
        if(g_reader_finish_num == reader_num){
            char time_str[64];
            sprintf(time_str, "elapsed_time:%ld\n", g_elapsed_time);
            write(trace_fd, time_str, strlen(time_str));
            usleep(1000000);
            g_reader_finish_num = 0;
            g_elapsed_time = 0;
            write(trace_fd, "FINISH\n", 7);
            //notice trace server;
        }
        pthread_mutex_unlock(&reader_finish_mutex);
    }
}

void *reader_thread(){
    reader_t *reader = create_reader();
    reader_t *reader_backup = create_reader();
    int conn = unix_domain_client_init(reader_proxy_path);
    reader_init(reader, conn);
    reader_init(reader_backup, conn);

    char txbuflen = 0;
    char txbuf[FRAME_MAX_LEN];
    memset(txbuf, 0, sizeof(txbuf));

    printf("conn:%d\n", conn);

    task_t *task = (task_t *)malloc(sizeof(task_t));
    char log_str[2048] = {0};
    char tmp_str[512] = {0};
    int index = 0;
    while(1){
        index++;
        memset(log_str, 0, sizeof(log_str));
        memset(task, 0, sizeof(task_t));
        int readlen = read(conn, (char *)task, sizeof(task_t));
        memset(tmp_str, 0, sizeof(tmp_str));
        sprintf(tmp_str, "[%ld-(%d)>>>>recv>>>", time(NULL), reader->conn);
        strcat(log_str, tmp_str);
        if(task->type == DOWNLINK_COLLISION){
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", "DOWNLINK_COLLISION>>>==");
            strcat(log_str, tmp_str);
            memcpy(reader, reader_backup, sizeof(reader_t));
            if(reader->state == DISCOVERY_REQUEST){
                 
            }else if(reader->state == QUERY_REQUEST){
                reader->elapsed_time += ENERGY_CHECK_TIME;
            }
//            downlink_collision_handler(reader);
        }else if(task->type == UPLINK_COLLISION){
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", "UPLINK_COLLISION>>>==");
            strcat(log_str, tmp_str);
            uplink_collision_handler(reader, task);
            reader->elapsed_time += DEFAULT_UPLINK_LEN * 8 * 1000 / UPLINK_BITRATE;
        }else if(task->type == UPLINK_DATA){
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "UPLINK_DATA>>> %02x %02x %02x %02x>>>==", task->buf[0], task->buf[1], task->buf[2], task->buf[3]);
            strcat(log_str, tmp_str);
            data_handler(reader, task);
            update_test_tab(reader, task->buf[3]);      //just for test
        }else if(task->type == TRIGGER){
            memcpy(reader_backup, reader, sizeof(reader_t));
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", "TRIGGER>>>==");
            strcat(log_str, tmp_str);
            task->type = ACTIVE;
            task->addr = conn;
            task->buflen = enframe(reader, task->buf);        
            reader->plen = task->buflen;
            task->reader_frame_state = reader->state;
            if(reader->state == DISCOVERY_REQUEST){
                task->backoff_time = gen_start_time();
                task->start_time = time(NULL) + task->backoff_time;
                reader->query_addr = 0;                
                reader->elapsed_time += task->backoff_time + (reader->plen * 8 * 1000/DOWNLINK_BITRATE) + UPLINK_ACK_TIME;
                memset(tmp_str, 0, sizeof(tmp_str));
                sprintf(tmp_str, "DISC-backoff_time:%d==", task->backoff_time);
//                sprintf(tmp_str, "[%ld->>>>send>>>", time(NULL));
                strcat(log_str, tmp_str);
            }else{
                task->backoff_time = 0;
                task->start_time = time(NULL) + task->backoff_time;
                reader->elapsed_time += task->backoff_time + (reader->plen * 8 * 1000/DOWNLINK_BITRATE);
            }
            write(conn, (char *)task, sizeof(task_t));        
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", "->>>>send>>>==");
            strcat(log_str, tmp_str);
            for(int i = 0; i < task->buflen; i++){
                memset(tmp_str, 0, sizeof(tmp_str));
                sprintf(tmp_str, " %02x", task->buf[i]);
                strcat(log_str, tmp_str);
            }
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", ">>>>==");
            strcat(log_str, tmp_str);
        }else if(task->type == UPLINK_IDLE){
            reader->elapsed_time += ENERGY_CHECK_TIME;
            uplink_idle_handler(reader, task);
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "%s", "UPLINK_IDLE>>>==");
            strcat(log_str, tmp_str);
        }else if(task->type == CARRIER_BLOCK){
            memset(tmp_str, 0, sizeof(tmp_str));
            sprintf(tmp_str, "reader:%d CARRIER_BLOCK>>>==", reader->addr);
            strcat(log_str, tmp_str);
            memcpy(reader, reader_backup, sizeof(reader_t));
            reader->elapsed_time += task->blocked_time - (reader->plen * 8 * 1000/DOWNLINK_BITRATE);
//            carrier_block_handler(reader); 
        }else if(task->type == RESET){
            printf("[%s]--RESET==", __func__);
            memset(reader, 0, sizeof(reader_t));
            reader_init(reader, conn);
            memset(reader_backup, 0, sizeof(reader_t));
            reader_init(reader_backup, conn);
            g_reader_finish_num = 0;
        }else if(task->type == KILL){
            break;
        }
        pthread_mutex_lock(&reader_mutex);
        printf("%s]\n", log_str);
        pthread_mutex_unlock(&reader_mutex);
    }
    free(task);
    destroy_reader(reader);
    destroy_reader(reader_backup);
}


void trigger_reader(reader_info_t *reader_info){
    printf("%s\n", __func__);
    task_t *task = (task_t *)malloc(sizeof(task_t));
    memset(task, 0, sizeof(task_t));
    task->type = TRIGGER;
    for(int i = 0; i < reader_num; i++){
        int len = write(reader_info[i].conn, (char *)task, sizeof(task_t));
        printf("trig send len:%d\n", len);
    }
    free(task);
}



//proxy define 
#define COLLISION   1
void *reader_proxy(){
    reader_info_t reader_info[READER_MAX_NUM];
    memset(reader_info, 0, sizeof(reader_info));
	int local_serverfd = unix_domain_server_init(reader_proxy_path);
	
	pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*reader_num);
    for(int i = 0; i < reader_num; i++){
        pthread_create(&thread_tab[i], NULL, reader_thread, NULL);
    }   


    int remote_clientfd = unix_domain_client_init(simulator_server_path);

    for(int i = 0; i < reader_num; i++){
        reader_info[i].conn = accept(local_serverfd, NULL, NULL);
        reader_info[i].vaild = 0;
        printf("accept:%d\n", reader_info[i].conn);
    }
    printf("init ok\n");
    int reset_flag = 1;
    int kill_flag = 0;
    int unresolved_flag = 0;
    while(1){
        if(reset_flag){
            reset_flag = 0;
            for(int i = 0; i < reader_num; i++){
                reader_info[i].vaild = 0;
                reader_info[i].start_time = 0; 
                reader_info[i].end_time = 0; 
                reader_info[i].backoff_time = 0; 
                reader_info[i].addr = 0; 
                reader_info[i].plen = 0; 
            }
        }
        if(kill_flag){
            break;
        }
//        sleep(1);
        trigger_reader(reader_info);
        while(1){
            fd_set fds;
            memset(&fds, 0, sizeof(fd_set));
            FD_SET(remote_clientfd, &fds);
            FD_SET(trace_fd, &fds);
            int maxfd = remote_clientfd > trace_fd ? remote_clientfd : trace_fd;
            for(int i = 0; i < reader_num; i++){
                FD_SET(reader_info[i].conn, &fds);
                maxfd = maxfd > reader_info[i].conn ? maxfd : reader_info[i].conn;
            }

            struct timeval tv;
            task_t task;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
            printf("ret:%d, unresolved_flag:%d\n", ret, unresolved_flag);
            if(ret > 0){
                if(FD_ISSET(trace_fd, &fds)){   //receive from trace server
                    char buf[64];
                    int buflen = 0;
                    buflen = read(trace_fd, buf, 64);
                    if(buflen >= 5 && (memcmp(buf, "RESET", 5) == 0)){
                        reset_flag = 1;
                        for(int i = 0; i < reader_num; i++){
                            printf("[%s]--RESET\n", __func__);
                            task.type = RESET;
                            write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                        }
                        break;
                    }else if(buflen >= 4 && (memcmp(buf, "KILL", 4) == 0)){
                        kill_flag = 1;
                        for(int i = 0; i < reader_num; i++){
                            task.type = KILL;
                            write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                        }
                        break;
                    }
                }

                if(FD_ISSET(remote_clientfd, &fds)){    //receive from tag, should not be happened.
                    printf("should not be tag data\n");
                    //recv_from_tag();
                }
                {   //receive from reader.
                    for(int i = 0; i < reader_num; i++){                    
                        if(FD_ISSET(reader_info[i].conn, &fds)){
                            read(reader_info[i].conn, (char *)&task, sizeof(task_t));
                            reader_info[i].start_time = task.start_time;
                            reader_info[i].backoff_time = task.backoff_time;
                            reader_info[i].end_time = reader_info[i].start_time + (reader_info[i].plen * 8)*1000/DOWNLINK_BITRATE;
//                            printf("s:%ld, e:%ld\n", reader_info[i].start_time, reader_info[i].end_time);
                            reader_info[i].addr = task.addr;
                            reader_info[i].plen = task.buflen;
                            reader_info[i].vaild = 1;
                            memcpy(reader_info[i].payload, task.buf, task.buflen);
                        }
                    }
                }
                unresolved_flag = 1;
            }else{                
                if(unresolved_flag == 1){
                    reader_info_t *reader_info_item = select_reader(reader_info);                
                    if(!reader_info_item){
                        task.type = DOWNLINK_COLLISION;     //downlink collision
                        for(int i = 0; i < reader_num; i++){
                            if(reader_info[i].vaild){   // send to reader
                                RECORD_TRACE(trace_fd, trace_buf, "%s\n", trace_type[task.type]);
                                write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                                reader_info[i].vaild = 0;
                            }
                        }
                    }else{
                        for(int i = 0; i < reader_num; i++){
                            if(reader_info[i].vaild){
                                if(reader_info_item != &reader_info[i] &&
                                reader_info_item->start_time > reader_info[i].start_time){
                                    task.type = DOWNLINK_COLLISION;  //downlink collision
                                    //send to reader
                                    RECORD_TRACE(trace_fd, trace_buf, "%s\n", trace_type[task.type]);
                                    write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                                }else if(reader_info_item != &reader_info[i] &&
                                reader_info_item->start_time < reader_info[i].start_time){  //carrier block
                                    task.type = CARRIER_BLOCK;
                                    task.blocked_time = DEFAULT_UPLINK_LEN * 8 * 1000/UPLINK_BITRATE - (reader_info[i].start_time - reader_info_item->start_time);
                                    //send to reader
                                    RECORD_TRACE(trace_fd, trace_buf, "%s\n", trace_type[task.type]);
                                    write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                                }
                                reader_info[i].vaild = 0;
                            }
                        }

                        //send to tag_proxy 
                        RECORD_TRACE(trace_fd, trace_buf, "%s", ">>>OUTPUT TO TAG_PROXY:");            
                        for(int i = 0; i < reader_info_item->plen; i++){
                            RECORD_TRACE(trace_fd, trace_buf, " %02x", reader_info_item->payload[i]);
                        }
                        RECORD_TRACE(trace_fd, trace_buf, "\n");            
                        write(remote_clientfd, reader_info_item->payload, reader_info_item->plen);  //send to tag_proxy
                        proxy_msg_t *proxy_msg = (proxy_msg_t *)malloc(sizeof(proxy_msg_t));
                        memset(proxy_msg, 0, sizeof(proxy_msg));
                        read(remote_clientfd, (char *)proxy_msg, sizeof(proxy_msg_t));
                        if(proxy_msg->type == UPLINK_COLLISION){
                            task.type = UPLINK_COLLISION;
                            RECORD_TRACE(trace_fd, trace_buf, "%s\n", trace_type[task.type]);
                            task.response_reader = reader_info_item->addr;
                        }else if(proxy_msg->type == UPLINK_DATA){
                            task.type = UPLINK_DATA;
                            task.response_reader = reader_info_item->addr;
                            task.buflen = proxy_msg->plen;
                            memcpy(task.buf, proxy_msg->payload, proxy_msg->plen);
                            RECORD_TRACE(trace_fd, trace_buf, "%s", "<<<RECEIVE FROM TAG_PROXY:");
                            for(int i = 0; i < proxy_msg->plen; i++){
                                RECORD_TRACE(trace_fd, trace_buf, " %02x", proxy_msg->payload[i]);
                            }
                            RECORD_TRACE(trace_fd, trace_buf, "\n");            
                        }else{  //UPLINK_IDLE
                            task.type = UPLINK_IDLE;
                            RECORD_TRACE(trace_fd, trace_buf, "%s\n", trace_type[task.type]);
                            task.response_reader = reader_info_item->addr;
                        }
                        free(proxy_msg);
                        for(int i = 0; i < reader_num; i++){
                            write(reader_info[i].conn, (char *)&task, sizeof(task_t));
                        }
                    }
                    unresolved_flag = 0;
                }else{
                    break;
                }
            }
        }
    }
    for(int i = 0; i < reader_num; i++){
        close(reader_info[i].conn); 
    }

    for(int i = 0; i < reader_num; i++){
        pthread_join(thread_tab[i], NULL);
    }   
    free(thread_tab);

    close(local_serverfd);
    close(remote_clientfd);

}


int main(int argc, char *argv[]){
    if(argc == 3){ 
        reader_num = atoi(argv[1]);
        tag_num = atoi(argv[2]);
    }else{
        usage(argv[0]);
        return 0;
    }   

    init_random();

    pthread_mutex_init(&reader_mutex, 0);

    trace_fd = unix_domain_client_init(trace_sock_path);

    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, reader_proxy, NULL);

/* 
    pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*reader_num);
    for(int i = 0; i < reader_num; i++){
        pthread_create(&thread_tab[i], NULL, reader_thread, NULL);
    }
*/
    pthread_join(thread_proxy, NULL);
/*    
    for(int i = 0; i < reader_num; i++){
        pthread_join(thread_tab[i], NULL);
    }
    free(thread_tab);
*/    
    destroy_random();
}
