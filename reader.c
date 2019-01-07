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


#define DOWN_SLOT_TIME   60
#define DOWNLINK_WINDOW 20
#define DOWNLINK_BITRATE 1000

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
}reader_t;

typedef struct{
    int type;
    int start_time;
    int addr;
    int response_reader;
    int plen;
    char payload[32];
    
}task_t;


typedef struct{
    int conn;
    int vaild;
    uint64_t start_time;
    uint64_t end_time;
    int addr;
    int plen;
    char payload[32];
}reader_info_t;

typedef struct{
    int type;
    int plen;
    char payload[32];
}proxy_msg_t;


int reader_num;
int tag_num;

reader_t reader_item_table[READER_MAX_NUM];



char *reader_proxy_path = "reader_proxy.sock";
char *simulator_server_path = "server.socket";

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


int gen_start_time(){
    int slot_time = DOWN_SLOT_TIME;
    int offset = random()%DOWN_SLOT_TIME;
    return offset + (random() % DOWNLINK_WINDOW) * DOWN_SLOT_TIME;
}

void enframe_discovery(reader_t *reader){
    reader->txbuf[0] = reader->downlink_frame.src;

    //reader->txbuf[1] = (reader->downlink_frame.type << 4);    
    reader->txbuf[1] = ((reader->downlink_frame.ack + DISCOVERY_REQUEST) << 4);

    printf("f_type:%d\n", reader->downlink_frame.type);
    reader->txbuf[1] += reader->downlink_frame.dst;
    reader->txbuf[2] = (reader->downlink_frame.collision_num << 4);
    reader->txbuf[2] += 0;
    reader->txbuflen = 3;
    printf("enframe:");
    for(int i = 0; i < reader->txbuflen; i++){
        printf("%02x ", reader->txbuf[i]);
    }
    printf("\n");
}


void enframe_query(reader_t *reader){
    reader->txbuf[0] = reader->downlink_frame.src;
    //reader->txbuf[1] = (reader->downlink_frame.type << 4);
    reader->txbuf[1] = ((reader->downlink_frame.ack + QUERY_REQUEST) << 4);
    reader->txbuf[1] += reader->downlink_frame.dst;
    reader->txbuflen = 2;
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

void readers_init(){
    for(int i = 0; i < reader_num; i++){
        memset(&reader_item_table[i], 0, sizeof(reader_t));
        reader_item_table[i].downlink_frame.src = i;
        //reader_item_table[i].downlink_frame.type = DISCOVERY_REQUEST_NACK;
        reader_item_table[i].downlink_frame.type = NACK;
        reader_item_table[i].downlink_frame.dst = 0x0F;
        reader_item_table[i].downlink_frame.collision_num = 0;
        reader_item_table[i].downlink_frame.plen = 0;
    }
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

void uplink_collision_handler(reader_t *reader, task_t *task){
    if(task->addr == reader->addr){ //for me
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

void data_handler(reader_t *reader, task_t *task){
    if(task->addr == reader->addr){
        reader->query_addr++;
        if(reader->query_addr == reader->tag_addr_range){
            reader->state = DISCOVERY_REQUEST;
            reader->ack = ACK;
            reader->query_addr = reader->tag_addr_range-1;            
            if(reader->collision_num){
                reader->tag_addr_range = 2*reader->collision_num;
            }else{
                reader->tag_addr_range = 1;
            }
        }else{
            reader->state = QUERY_REQUEST;
            reader->ack = ACK;            
        }
    }
}

void uplink_idle_handler(reader_t *reader, task_t *task){
    if(task->addr == reader->addr){
        reader->query_addr++;
        printf("---tag_range:%d\n", reader->tag_addr_range);
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
    memset(reader->pair, 0, sizeof(reader->pair));
    reader->plen = 0;
    memset(reader->payload, 0, sizeof(reader->payload));
}

int enframe(reader_t *reader, char *txbuf){
    int txbuflen = 0;
    txbuf[0] = reader->addr;
    txbuf[1] = ((reader->state + reader->ack) << 4);
    txbuf[1] += reader->query_addr;
    if(reader->state == DISCOVERY_REQUEST){
        txbuf[2] = (reader->collision_num << 4);
        txbuf[2] += reader->plen;
        memcpy(&txbuf[3], reader->payload, reader->plen);
        txbuflen = 3+reader->plen;
        reader->collision_num = 0;
    }else{
        txbuflen = 2;
    }
    return txbuflen;
}




//Task define 
#define ACTIVE              0
#define UPLINK_COLLISION    1
#define UPLINK_IDLE         2
#define UPLINK_DATA         3
#define DOWNLINK_COLLISION  4
#define TRIGGER             5

void *reader_thread(){
    reader_t *reader = create_reader();
    int conn = unix_domain_client_init(reader_proxy_path);
    reader_init(reader, conn);

    char txbuflen = 0;
    char txbuf[FRAME_MAX_LEN];
    memset(txbuf, 0, sizeof(txbuf));

    printf("conn:%d\n", conn);

    task_t *task = (task_t *)malloc(sizeof(task_t));
    while(1){
        memset(task, 0, sizeof(task_t));
        int readlen = read(conn, (char *)task, sizeof(task_t));
        printf("[%ld->>>>recv>>>", time(NULL));
        if(task->type == DOWNLINK_COLLISION){
            downlink_collision_handler(reader);
        }else if(task->type == UPLINK_COLLISION){
            printf("UPLINK_COLLISION>>>]\n");
            uplink_collision_handler(reader, task);
        }else if(task->type == UPLINK_DATA){
            printf("UPLINK_DATA>>>]\n");
            data_handler(reader, task);
        }else if(task->type == TRIGGER){
            printf("TRIGGER>>>]\n");
            task->type = ACTIVE;
            task->start_time = time(NULL) + gen_start_time();
            task->addr = conn;
            task->plen = enframe(reader, task->payload);        
            write(conn, (char *)task, sizeof(task_t));        
            if(reader->state == DISCOVERY_REQUEST){
                reader->query_addr = 0;
            }
            printf("[%ld->>>>send>>>", time(NULL));
            for(int i = 0; i < task->plen; i++){
                printf(" %02x", task->payload[i]);
            }
            printf(">>>>]\n");
        }else if(task->type == UPLINK_IDLE){
            uplink_idle_handler(reader, task);
            printf("UPLINK_IDLE>>>]\n");
        }
    }
    destroy_reader(reader);
}


void trigger_reader(reader_info_t *reader_info){
    task_t *task = (task_t *)malloc(sizeof(task_t));
    memset(task, 0, sizeof(task_t));
    task->type = TRIGGER;
    for(int i = 0; i < reader_num; i++){
        write(reader_info[i].conn, (char *)task, sizeof(task_t));
    }
    free(task);
}



//proxy define 
#define COLLISION   1
void *reader_proxy(){
    reader_info_t reader_info[READER_MAX_NUM];
    memset(reader_info, 0, sizeof(reader_info));
	int local_serverfd = unix_domain_server_init(reader_proxy_path);
    int remote_clientfd = unix_domain_client_init(simulator_server_path);

    for(int i = 0; i < reader_num; i++){
        reader_info[i].conn = accept(local_serverfd, NULL, NULL);
        reader_info[i].vaild = 0;
        printf("accept:%d\n", reader_info[i].conn);
    }
    printf("init ok\n");
    while(1){
        sleep(1);
        trigger_reader(reader_info);
        while(1){
            fd_set fds;
            memset(&fds, 0, sizeof(fd_set));
            FD_SET(remote_clientfd, &fds);
            int maxfd = remote_clientfd;
            for(int i = 0; i < reader_num; i++){
                FD_SET(reader_info[i].conn, &fds);
                maxfd = maxfd > reader_info[i].conn ? maxfd : reader_info[i].conn;
            }

            struct timeval tv;
            task_t task;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
            if(ret > 0){
                if(FD_ISSET(remote_clientfd, &fds)){
                    printf("should not be tag data\n");
                    //recv_from_tag();
                }else{
                    for(int i = 0; i < reader_num; i++){                    
                        if(FD_ISSET(reader_info[i].conn, &fds)){
                            read(reader_info[i].conn, (char *)&task, sizeof(task_t));
                            reader_info[i].start_time = task.start_time;
                            reader_info[i].addr = task.addr;
                            reader_info[i].plen = task.plen;
                            reader_info[i].vaild = 1;
                            memcpy(reader_info[i].payload, task.payload, task.plen);
                        }
                    }
                }
            }else{                
                reader_info_t *reader_info_item = select_reader(reader_info);                
                if(!reader_info_item){
                    task.type = DOWNLINK_COLLISION;
                    for(int i = 0; i < reader_num; i++){
                        write(reader_info[i].conn, (task_t *)&task, sizeof(task_t));
                    }
                }else{
                    write(remote_clientfd, reader_info_item->payload, reader_info_item->plen);
                    proxy_msg_t *proxy_msg = (proxy_msg_t *)malloc(sizeof(proxy_msg_t));
                    memset(proxy_msg, 0, sizeof(proxy_msg));
                    read(remote_clientfd, (char *)proxy_msg, sizeof(proxy_msg_t));
                    if(proxy_msg->type == UPLINK_COLLISION){
                        task.type = UPLINK_COLLISION;
                        task.response_reader = reader_info_item->addr;
                    }else if(proxy_msg->type == UPLINK_DATA){
                        task.type = UPLINK_DATA;
                        task.response_reader = reader_info_item->addr;
                        task.plen = proxy_msg->plen;
                        memcpy(task.payload, proxy_msg->payload, proxy_msg->plen);
                    }else{  //UPLINK_IDLE
                        task.type = UPLINK_IDLE;
                        task.response_reader = reader_info_item->addr;
                    }
                    free(proxy_msg);
                    for(int i = 0; i < reader_num; i++){
                        write(reader_info[i].conn, (task_t *)&task, sizeof(task_t));
                    }
                }
                break;
            }
        }
    }

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
    
    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, reader_proxy, NULL);

    sleep(1);
    pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*reader_num);
    for(int i = 0; i < reader_num; i++){
        pthread_create(&thread_tab[i], NULL, reader_thread, NULL);
    }
    while(1);
}
