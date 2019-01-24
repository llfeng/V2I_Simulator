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

#include <algorithm>
#include <vector>
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
#include <math.h>
#include "common.h"
#include "log.h"
#include "is_available.h"


using namespace std;


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
//    char tag_addr_range;
    char collision_num;
    char plen;
    char payload[PAYLOAD_MAX_LEN];
    char pair_num;
    addr_pair_t pair[TAG_MAX_NUM];
    int start_time;
    int init_time;
    int recv_tag_num;
    char test_tab[TAG_MAX_NUM];
    uint64_t elapsed_time;    
    int posx;
    int posy;
    double velocity;
    int carrier_block_time;
    char last_collision_num;
    char last_query_addr;
    char last_round;
    char last_pair_num;
    addr_pair_t last_pair[TAG_MAX_NUM];
    char received_id[TAG_MAX_NUM];
    char received_num;
}reader_t;

typedef struct{
    int type;
    int reader_frame_state;
    long int start_time;
    int backoff_time;
    int blocked_time;
    int elapsed_time;
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
    int elapsed_time;
    int addr;
    int plen;
    char payload[32];
}reader_info_t;

#if 0
typedef struct{
    int type;
    int plen;
    int start_time;
    char payload[32];
}tag_response_t;

typedef struct{
    int conn;
    int posx;
    int posy;
    int plen;
    int start_time;
    int backoff_time;
    int elapsed_time;
    char payload[32];
}reader_request_t;                     
#endif

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

pthread_mutex_t g_write_log_mutex;

int g_reader_finish_num = 0;

uint64_t g_elapsed_time = 0;

int reader_num;
int tag_num;
int g_lane_num;
double g_velocity;         //unit--m/ms

double g_com_dist;
double g_sys_fov;


int g_car_num_per_lane;
int g_car_dist;


reader_t reader_item_table[READER_MAX_NUM];

int trace_fd = -1;

char trace_buf[512] = {0};

const char *trace_sock_path = "trace.sock";
const char *reader_proxy_path = "reader_proxy.sock";
const char *simulator_server_path = "server.socket";

//const char *log_path = "V2I.log";
const char *res_path = "result.csv" ;
int g_log_file_fd = -1;
int g_res_file_fd = -1;


int gen_init_time(){
    int slot_time = DOWN_SLOT_TIME;
    int offset = get_random()%DOWN_SLOT_TIME;
    return offset + (get_random() % DOWNLINK_WINDOW) * DOWN_SLOT_TIME;
}


int gen_start_time(){
    int slot_time = DOWN_SLOT_TIME;
    int offset = 0;
    return offset + (get_random() % DOWNLINK_WINDOW) * DOWN_SLOT_TIME;
}


bool reader_request_cmp_descent(reader_request_t a, reader_request_t b){
    return a.start_time > b.start_time;
}

void select_reader(req_bat_t *ready_req_bat, req_bat_t *sent_req_bat){
    sort(ready_req_bat->req, ready_req_bat->req + reader_num, reader_request_cmp_descent);
    for(int i = reader_num - 1; i >= 0; i--){
        if(ready_req_bat->req[reader_num - 1].start_time + (ready_req_bat->req[reader_num - 1].plen << 3)*1000/DOWNLINK_BITRATE < ready_req_bat->req[i].start_time){
            break;
        }else{            
            memcpy(&sent_req_bat->req[sent_req_bat->num++], &ready_req_bat->req[i], sizeof(reader_request_t));
        }
    }
    ready_req_bat->num = reader_num - sent_req_bat->num;
}

#if 0
int select_reader(reader_request_t **reader_request_tbl, reader_request_t **sent_request_tbl){
    int sent_num = 0;
    sort(reader_request_tbl, reader_request_tbl+reader_num, reader_request_cmp_descent);
    for(int i = reader_num - 1; i >= 0; i--){        
        if(reader_request_tbl[reader_num - 1]->start_time + (reader_request_tbl[reader_num - 1]->plen << 3)*1000/DOWNLINK_BITRATE < reader_request_tbl[i]->start_time){
            break;
        }else{
            sent_request_tbl[sent_num++] = reader_request_tbl[i];
        }
    }

    for(int i = 1; i <= sent_num; i++){
        reader_request_tbl[reader_num - i] = NULL;
    }

    return sent_num;
}
#endif


reader_t *create_reader(){
    reader_t *reader = (reader_t *)malloc(sizeof(reader_t));
    memset(reader, 0, sizeof(reader_t));
    return reader;
}

void destroy_reader(reader_t *reader){
    free(reader);
}


void downlink_collision_handler(reader_t *reader){
    reader->start_time += UPLINK_ACK_TIME;
    reader->start_time += gen_start_time();
//do nothing, just wait for retransmission
}

void carrier_block_handler(reader_t *reader){
//do nothing.
}



int get_tag_addr_range(reader_t *reader){
    int tag_addr_range = 0;
    if(reader->last_collision_num){
        tag_addr_range = 2*reader->last_collision_num;
    }else{
        tag_addr_range = 1;
    }
    return tag_addr_range;
}


void uplink_collision_handler(reader_t *reader){
    reader->collision_num++;
    reader->query_addr++;
    int tag_addr_range = get_tag_addr_range(reader);
    if(reader->query_addr == tag_addr_range){
        reader->state = DISCOVERY_REQUEST;            
        reader->round++;
        reader->ack = CACK;
        reader->query_addr = tag_addr_range;            
        reader->start_time = reader->carrier_block_time + gen_start_time();
    }else{
        reader->state = QUERY_REQUEST;
        reader->ack = CACK;            
        reader->start_time = reader->carrier_block_time; 
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


char get_tag_uuid(tag_response_t *tag_response){
    return tag_response->payload[3];
}

int updata_receive_id_table(reader_t *reader, tag_response_t *tag_response){
    int exist_flag = 0;
    char tag_uuid = get_tag_uuid(tag_response);
    for(int i = 0; i < reader->received_num; i++){
        if(reader->received_id[i] == tag_uuid){
            exist_flag = 1;
            break;
        }
    }
    if(exist_flag){
        return 1;
    }else{
        reader->received_id[reader->received_num++] = tag_uuid;
        return 0;
    }
}

void uplink_data_handler(reader_t *reader, tag_response_t *tag_response){
    int com_x = sqrt(pow(g_com_dist, 2) - pow(reader->posy,2));
    int delta_t = tag_response->start_time + (tag_response->plen << 3) * 1000/UPLINK_BITRATE - reader->init_time;
    double x = reader->posx + delta_t * reader->velocity;
    char log_s[512];
    pthread_mutex_lock(&g_write_log_mutex);
    if(updata_receive_id_table(reader, tag_response) == 0){
        RECORD_TRACE(g_log_file_fd, log_s, "time:%d--tag[%d](%d, %d) ---> reader[%d](%f, %d)\n", delta_t + reader->init_time, get_tag_uuid(tag_response), tag_response->posx, tag_response->posy, reader->addr, x, reader->posy);
    }
    pthread_mutex_unlock(&g_write_log_mutex);

//    int real_x = tag_response->posx

    reader->query_addr++;
    int tag_addr_range = get_tag_addr_range(reader);
    if(reader->query_addr == tag_addr_range){
        reader->state = DISCOVERY_REQUEST;
        reader->round++;
        reader->ack = ACK;
        reader->query_addr = tag_addr_range;
        reader->start_time = reader->carrier_block_time + gen_start_time();
    }else{  
        reader->state = QUERY_REQUEST;
        reader->ack = ACK;            
        reader->start_time = reader->carrier_block_time;
    }
}

bool tag_response_cmp_ascent(tag_response_t a, tag_response_t b){
    return a.start_time < b.start_time; 
}

bool tag_response_end_cmp_ascent(tag_response_t a, tag_response_t b){
    return a.start_time + (a.plen << 3) * 1000/UPLINK_BITRATE < b.start_time + (b.plen << 3) * 1000/UPLINK_BITRATE; 
}



void piggyback_data_handler(reader_t *reader, tag_response_t *tag_response_tbl, int rsp_num){       //not for me
    if(rsp_num == 1){
        if(lookup_pair(reader, tag_response_tbl[0].payload[0], tag_response_tbl[0].payload[1] >> 4) == 0){
            reader->pair[reader->pair_num].reader = tag_response_tbl[0].payload[0];
            reader->pair[reader->pair_num].tag = (tag_response_tbl[0].payload[1] >> 4);
            reader->pair[reader->pair_num].round = (tag_response_tbl[0].payload[2]);
            reader->pair_num++;

            int com_x = sqrt(pow(g_com_dist, 2) - pow(reader->posy,2));
            int delta_t = tag_response_tbl[0].start_time + (tag_response_tbl[0].plen << 3) * 1000/UPLINK_BITRATE - reader->init_time;
            double x = reader->posx + delta_t * reader->velocity;
            char log_s[512];
            pthread_mutex_lock(&g_write_log_mutex);
            if(updata_receive_id_table(reader, &tag_response_tbl[0]) == 0){
                RECORD_TRACE(g_log_file_fd, log_s, "time:%d--tag[%d](%d, %d) ---> reader[%d](%f, %d)\n", delta_t + reader->init_time, get_tag_uuid(&tag_response_tbl[0]), tag_response_tbl[0].posx, tag_response_tbl[0].posy, reader->addr, x, reader->posy);
            }
            pthread_mutex_unlock(&g_write_log_mutex);
        }
    }else if(rsp_num == 2){
        sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_cmp_ascent);
        if(((tag_response_tbl[0].type == DISC_ACK) && (tag_response_tbl[1].type == DATA))){     //piggyback
            if(lookup_pair(reader, tag_response_tbl[1].payload[0], tag_response_tbl[1].payload[1] >> 4) == 0){
                reader->pair[reader->pair_num].reader = tag_response_tbl[1].payload[0];
                reader->pair[reader->pair_num].tag = (tag_response_tbl[1].payload[1] >> 4);
                reader->pair[reader->pair_num].round = (tag_response_tbl[1].payload[2]);
                reader->pair_num++;

                int com_x = sqrt(pow(g_com_dist, 2) - pow(reader->posy,2));
                int delta_t = tag_response_tbl[1].start_time + (tag_response_tbl[1].plen << 3) * 1000/UPLINK_BITRATE - reader->init_time;
                double x = reader->posx + delta_t * reader->velocity;
                char log_s[512];
                pthread_mutex_lock(&g_write_log_mutex);
                if(updata_receive_id_table(reader, &tag_response_tbl[1]) == 0){
                    RECORD_TRACE(g_log_file_fd, log_s, "time:%d--tag[%d](%d, %d) ---> reader[%d](%f, %d)\n", delta_t + reader->init_time, get_tag_uuid(&tag_response_tbl[1]), tag_response_tbl[1].posx, tag_response_tbl[1].posy, reader->addr, x, reader->posy);
                }
                pthread_mutex_unlock(&g_write_log_mutex);
            }
        }else{          //uplink collision

        }
    }
    if(reader->state == DISCOVERY_REQUEST){
        reader->start_time = reader->carrier_block_time + gen_start_time();
    }else{
        reader->start_time = reader->carrier_block_time;
    }
}

void handler_piggyback_response(tag_response_t *tag_response_tbl, reader_t *reader, int rsp_num){
    sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_end_cmp_ascent);
    reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE;
    piggyback_data_handler(reader, tag_response_tbl, rsp_num);
}

void uplink_idle_handler(reader_t *reader){
    reader->query_addr++;
    int tag_addr_range = 0;
    if(reader->last_collision_num){
        tag_addr_range = 2*reader->last_collision_num;
    }else{
        tag_addr_range = 1;
    }
    if(reader->query_addr == tag_addr_range){
        reader->state = DISCOVERY_REQUEST;
        reader->ack = NACK;
        reader->query_addr = tag_addr_range;            
        reader->start_time += ENERGY_CHECK_TIME;
        reader->start_time += gen_start_time();
    }else{
        reader->state = QUERY_REQUEST;
        reader->ack = NACK;            
        reader->start_time += ENERGY_CHECK_TIME;
    }
}


void uplink_ack_handler(reader_t *reader){
    return;
}

//如果有连续两个idle slot,就有可能发生downlink冲突(QUERY和DISCOVERY)


void reader_init(reader_t *reader, int conn, int *arg_buf){
    reader->conn = conn;
    reader->addr = conn;
    reader->state = DISCOVERY_REQUEST;
    reader->ack = NACK;
    reader->query_addr = 0;
    reader->query_addr = 0;

    reader->posx = arg_buf[0];
    reader->posy = arg_buf[1];

//    reader->tag_addr_range = 1;
    reader->collision_num = 0;
    reader->last_collision_num = 0;
    reader->pair_num = 0;
    reader->round = 0;
    memset(reader->pair, 0, sizeof(reader->pair));
    reader->plen = 0;
    memset(reader->payload, 0, sizeof(reader->payload));
    reader->start_time += gen_init_time();
    reader->velocity = g_velocity;
}

#if 1
int enframe(reader_t *reader, char *txbuf){
    int txbuflen = 0;
    txbuf[0] = reader->addr;
    txbuf[1] = ((reader->state + reader->ack) << 4);
    txbuf[1] += reader->query_addr;
    if(reader->state == DISCOVERY_REQUEST){
        txbuf[2] = reader->round;
        txbuf[3] = (reader->collision_num << 4);
        txbuf[3] += reader->pair_num * 3;        
        for(int i = 0; i < reader->pair_num; i++){
            txbuf[i*3+4] = reader->pair[i].reader;
            txbuf[i*3+5] = reader->pair[i].tag;
            txbuf[i*3+6] = reader->pair[i].round;
//            reader->pair[i].reader = 0;
//            reader->pair[i].tag = 0;
//            reader->pair[i].round = 0;
        }
        txbuflen = 4 + reader->pair_num * 3;
        txbuf[txbuflen++] = 0;
//        reader->pair_num = 0;
//        reader->collision_num = 0;
    }else{
        txbuf[2] = reader->round;
        txbuf[3] = 0;
        txbuflen = 4;
    }
    return txbuflen;
}
#else
int enframe(reader_t *reader, char *txbuf){
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
}
#endif

#if 0
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
#endif

int be_blocked(reader_request_t *reader_request, tag_response_t *tag_response){
    return 0;
}

/* 
void get_send_pos(reader_request_t *reader_request, int *x, int *y){
    *x = reader_request->posx + (reader_request->start_time - reader_request->init_time) * reader_request->velocity;
    *y = reader_request->posy;
}

void get_recv_pos(reader_request_t *reader_request, tag_response_t *tag_response, int *x, int *y){
    *x = reader_request->posx + (tag_response->start_time + (tag_response->plen << 3)*1000/DOWNLINK_BITRATE - reader_request->init_time) * reader_request->velocity;
    *y - reader_request->posy;
}
*/


#if 1
int in_range(reader_request_t *reader_request, tag_response_t *tag_response){
//    return 1;
    //distance
    //fov
    //blockage
    int cur_posx = reader_request->posx + (tag_response->start_time + (tag_response->plen << 3)*1000/DOWNLINK_BITRATE - reader_request->init_time) * reader_request->velocity;
    //double delta_x = reader_request->posx  - tag_response->posx;
    double delta_x = cur_posx  - tag_response->posx;
    if(delta_x > 0){
        return 0;
    }
    double delta_y = reader_request->posy - tag_response->posy;
    double distance = sqrt(pow(delta_x, 2) + pow(delta_y, 2));
    double degree = atan(fabs(delta_y)/fabs(delta_x));
    LOG(DEBUG, "[%d]--tag(%d, %d)-->reader(%d,%d), distance:%lf, degree:%lf, g_com_dist:%lf, g_sys_fov:%lf", reader_request->addr, tag_response->posx, tag_response->posy,
    cur_posx, reader_request->posy,distance,degree, g_com_dist, g_sys_fov);
    if(distance < g_com_dist && degree < g_sys_fov){
        if(be_blocked(reader_request, tag_response)){
            return 0;
        }else{
            return 1;
        }
    }else{
        return 0;
    }
}
#else
int in_range(req_bat_t *req_bat, reader_request_t *reader_request, tag_response_t *tag_response){
    vector<Point> lights;
    for(int i = 0; i < req_bat->num; i++){
        int x,y;
        get_recv_pos(&req_bat->req[i], tag_response, &x, &y);
        lights.push_back(Point(x, y));
    }
    for(int i = 0; i < req_bat->num; i++){
        int x,y;
        get_recv_pos(&req_bat->req[i], tag_response, &x, &y);
        lights.push_back(Point(x, y));
    }
}
#endif

char get_dst_addr(tag_response_t *rsp){
    return rsp->payload[0];
}

char get_src_addr(tag_response_t *rsp){
    return (rsp->payload[1] >> 4);
}

void handler_tag_response(tag_response_t *tag_response_tbl, reader_t *reader, int rsp_num){
    sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_cmp_ascent);
    if(rsp_num > 2){
        //UPLINK COLLISION
        sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_end_cmp_ascent);
        reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE;
        uplink_collision_handler(reader); 
    }else if(rsp_num == 2){
            if(((tag_response_tbl[0].type == DISC_ACK) && (tag_response_tbl[1].type == DATA))){
                //UPLINK DATA
                reader->carrier_block_time = tag_response_tbl[1].start_time + (tag_response_tbl[1].plen<<3)*1000/UPLINK_BITRATE;
                if(get_dst_addr(&tag_response_tbl[1]) == reader->addr){
                    uplink_ack_handler(reader);
                    uplink_data_handler(reader, &tag_response_tbl[1]);
                }else{
                    piggyback_data_handler(reader, &tag_response_tbl[1], 1);
                }
            }else{
                //UPLINK COLLISION
                sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_end_cmp_ascent);
                reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE;
                uplink_collision_handler(reader);
            }
    }else{
        reader->carrier_block_time = tag_response_tbl[0].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE;
//        LOG(INFO, "carrier_block_time:%d, tag_response_tbl[0].start_time:%d, tag_response_tbl[rsp_num - 1].plen:%d", reader->carrier_block_time, tag_response_tbl[0].start_time, tag_response_tbl[rsp_num - 1].plen);
        if(get_dst_addr(&tag_response_tbl[0]) == reader->addr){
            uplink_data_handler(reader, &tag_response_tbl[0]);
        }else{
            piggyback_data_handler(reader, &tag_response_tbl[0], 1);
        }
    }
}

reader_request_t *gen_reader_request(reader_t *reader){
    reader_request_t *reader_request = (reader_request_t *)malloc(sizeof(reader_request_t));
    reader_request->conn = -1;
    reader_request->addr = reader->addr;
    reader_request->posx = reader->posx;
    reader_request->posy = reader->posy;
    reader_request->velocity = reader->velocity;
    reader_request->start_time = reader->start_time;
    reader_request->init_time = reader->init_time;
    reader_request->plen = enframe(reader, reader_request->payload);

#if 0
    LOG(INFO, "reader start:%d", reader_request->start_time);
    char tmp[128];
    for(int i = 0; i < reader_request->plen; i++){
        sprintf(tmp + i*3, "%02x ", reader_request->payload[i]);
    }
    LOG(INFO, "send payload:%s", tmp);
#endif    
    return reader_request;
}

//round
//collision_num
//query_addr
//pair_num
//pair


void reader_backup(reader_t *reader){
    reader->last_collision_num = reader->collision_num;
    reader->last_query_addr = reader->query_addr;
    reader->last_round = reader->round;
    reader->last_pair_num = reader->pair_num;
    memcpy(reader->last_pair, reader->pair, sizeof(addr_pair_t));
}


#define RIGHT_TAG 200

//init_posx
//init_posy
void send_reader_request_to_reader_proxy(reader_t *reader){
    int cur_posx = reader->posx + (reader->start_time - reader->init_time) * reader->velocity;
    if( cur_posx > RIGHT_TAG){    
        int start_time;
        int pos_buf[2];        
        start_time = reader->start_time;
        pos_buf[0] = reader->posx + (reader->start_time - reader->init_time) * reader->velocity - g_car_num_per_lane * g_car_dist;
        pos_buf[1] = reader->posy;
        reader_init(reader, reader->conn, pos_buf);
        reader->init_time = start_time;
    }
//    LOG(INFO, "[%d]---collision_num:%d", reader->conn, reader->collision_num);
    reader_request_t *reader_request = gen_reader_request(reader);    
    LOG(INFO, "[%d]---velocity:%f, start_time:%d", reader->conn, reader_request->velocity, reader_request->start_time);
    int sendlen = write(reader->conn, (char *)reader_request, sizeof(reader_request_t));
    if(reader->state == DISCOVERY_REQUEST){
        reader_backup(reader);

        reader->query_addr = 0;
        reader->collision_num = 0;
//        reader->round;
        reader->pair_num = 0;
        memset(reader->pair, 0, sizeof(addr_pair_t));
    }
    free(reader_request);
}

void reader_restore(reader_t *reader){
    reader->collision_num = reader->last_collision_num;
    reader->query_addr = reader->last_query_addr;
    reader->round = reader->last_round;
    reader->pair_num = reader->last_pair_num;
    memcpy(reader->pair, reader->last_pair, sizeof(addr_pair_t));
}



void handler_no_response(reader_t *reader){
    if(reader->state == DISCOVERY_REQUEST){   //DOWNLINK COLLISION  @retransmit
        reader->start_time += UPLINK_ACK_TIME;
        reader->start_time += gen_start_time();
        reader_restore(reader);
    }else if(reader->state == QUERY_REQUEST){ //UPLINK IDLE
        reader->query_addr++;
        int tag_addr_range = get_tag_addr_range(reader);
        if(reader->query_addr == tag_addr_range){
            reader->state = DISCOVERY_REQUEST;
            reader->round++;
            reader->ack = NACK;
            reader->query_addr = tag_addr_range;            
            reader->start_time += ENERGY_CHECK_TIME;
            reader->start_time += gen_start_time();
        }else{
            reader->state = QUERY_REQUEST;
            reader->ack = NACK;            
            reader->start_time += ENERGY_CHECK_TIME;
        }
    }
}


void *reader_thread(void *arg){
    reader_t *reader = create_reader();
    int conn = unix_domain_client_init(reader_proxy_path);

    int *arg_buf = (int *)arg;
    reader_init(reader, conn, arg_buf);

    printf("conn:%d, (%d, %d)\n", conn, reader->posx, reader->posy);

    char log_str[2048] = {0};
    char tmp_str[512] = {0};

    reader_request_t *reader_request = (reader_request_t *)malloc(sizeof(reader_request_t));
    memset(reader_request, 0, sizeof(reader_request_t));

    tag_response_t *tag_response_tbl[TAG_MAX_NUM] = {NULL};
    rsp_bat_t *tag_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
    while(1){
        int rsp_num = 0;
        
        memset(tag_rsp_bat, 0, sizeof(rsp_bat_t));

        read(conn, (char *)tag_rsp_bat, sizeof(rsp_bat_t));

        if(tag_rsp_bat->type == INIT_TRIGGER){
            send_reader_request_to_reader_proxy(reader);
            LOG(INFO, "TI+RIIGER");
        }else{
            LOG(INFO, "tag_rsp_bat->sent:%d", tag_rsp_bat->sent);
            if(tag_rsp_bat->sent){  //maybe for me
                LOG(DEBUG, "i am %d, maybe for me, rsp_num:%d", reader->addr, tag_rsp_bat->num);
                if(tag_rsp_bat->num == 0){  //no uplink (DOWNLINK COLLISION or UPLINK IDLE or BLOCKAGE)
                    handler_no_response(reader);        
                }else{
                    handler_tag_response(tag_rsp_bat->rsp, reader, tag_rsp_bat->num);                 //UPLINK DATA or UPLINK COLLISION or DISC_ACK 
                }            
                send_reader_request_to_reader_proxy(reader);
            }else{      //piggyback
                if(tag_rsp_bat->num == 0) {
                    if(reader->state == DISCOVERY_REQUEST){
                        reader_restore(reader);
                    }
                    //do nothing
                }else{  
                    if(reader->state == DISCOVERY_REQUEST){
                        reader_restore(reader);
                    }
//                    LOG(DEBUG, "i am %d, not for me, but piggyback, rsp_num:%d", reader->addr, tag_rsp_bat->num);
                    handler_piggyback_response(tag_rsp_bat->rsp, reader, tag_rsp_bat->num);         //UPLINK DATA or UPLINK COLLISION or DISC_ACK
                }
                send_reader_request_to_reader_proxy(reader);
            }
        }
#if 0
        pthread_mutex_lock(&reader_mutex);
        printf("%s]\n", log_str);
        pthread_mutex_unlock(&reader_mutex);
#endif        
    }
    free(tag_rsp_bat);
    destroy_reader(reader);
}


void trigger_reader(int *reader_conn){
    printf("%s\n", __func__);
    rsp_bat_t *rsp = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
    rsp->type = INIT_TRIGGER;
    for(int i = 0; i < reader_num; i++){
        int len = write(reader_conn[i], (char *)rsp, sizeof(rsp_bat_t));
        printf("init send len:%d\n", len);
    }
    free(rsp);
}

#if 1
void do_handler(req_bat_t *sent_req_bat, 
                req_bat_t *ready_req_bat, 
                rsp_bat_t *rsp_bat){ 

    for(int i = 0; i < sent_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 1;
        to_reader_rsp_bat->type = TAG_RSP;

        for(int j = 0; j < rsp_bat->num; j++){      //
            vector<Point> lights;
            for(int k = 0; k < sent_req_bat->num; k++){
                int x,y;
                get_recv_pos(&sent_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            for(int k = 0; k < ready_req_bat->num; k++){
                int x,y;
                get_recv_pos(&ready_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            int x,y;
            get_recv_pos(&sent_req_bat->req[i], &rsp_bat->rsp[j], &x, &y);                
            lights.push_back(Point(x, y)); 
            if(in_range(&sent_req_bat->req[i], &rsp_bat->rsp[j])){                
//            if(in_range(&sent_req_bat->req[i], &rsp_bat->rsp[j]) && !is_intersect(lights, Point(rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy), lights.size())){                
                LOG(INFO, "[%d] is in range.", sent_req_bat->req[i].addr);
                memcpy(&to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++], &rsp_bat->rsp[j], sizeof(tag_response_t)); 
            }
        }

        LOG(INFO, "send to [%d]", sent_req_bat->req[i].addr);
        write(sent_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }

    for(int i = 0; i < ready_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 0;
        to_reader_rsp_bat->type = TAG_RSP;

        for(int j = 0; j < rsp_bat->num; j++){
            vector<Point> lights;
            for(int k = 0; k < sent_req_bat->num; k++){
                int x,y;
                get_recv_pos(&sent_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            for(int k = 0; k < ready_req_bat->num; k++){
                int x,y;
                get_recv_pos(&ready_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            int x,y;
            get_recv_pos(&ready_req_bat->req[i], &rsp_bat->rsp[j], &x, &y);
            lights.push_back(Point(x, y));
            if(in_range(&ready_req_bat->req[i], &rsp_bat->rsp[j])){                
//            if(in_range(&ready_req_bat->req[i], &rsp_bat->rsp[j]) && !is_intersect(lights, Point(rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy), lights.size())){                
                LOG(INFO, "[%d] is in range.", ready_req_bat->req[i].addr);
                memcpy(&to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++], &rsp_bat->rsp[j], sizeof(tag_response_t)); 
            }
        }
        write(ready_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }
    sent_req_bat->num = 0;
    ready_req_bat->num = 0;
}

#else
void do_handler(req_bat_t *sent_req_bat, 
                req_bat_t *ready_req_bat, 
                rsp_bat_t *rsp_bat){ 
    for(int i = 0; i < sent_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 1;
        for(int j = 0; j < rsp_bat->num; j++){
            if(in_range(&sent_req_bat->req[i], &rsp_bat->rsp[j])){                
                LOG(INFO, "[%d] is in range.", sent_req_bat->req[i].addr);
                memcpy(&to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++], &rsp_bat->rsp[j], sizeof(tag_response_t)); 
            }
        }
        write(sent_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }
    sent_req_bat->num = 0;

    for(int i = 0; i < ready_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 0;
        for(int j = 0; j < rsp_bat->num; j++){
            if(in_range(&ready_req_bat->req[i], &rsp_bat->rsp[j])){
                LOG(INFO, "[%d] is in range.", sent_req_bat->req[i].addr);
                memcpy(&to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++], &rsp_bat->rsp[j], sizeof(tag_response_t)); 
            }
        }
        write(ready_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }
    ready_req_bat->num = 0;
}
#endif

int is_blocked(tag_response_t *rsp){
    return 0;
}


#define LANE_MAX_NUM    5
#define MAX_CAR_PER_LANE    50

//proxy define 
#define COLLISION   1
void *reader_proxy(void *arg){

    int reader_conn[READER_MAX_NUM];    
    memset(reader_conn, 0, sizeof(reader_t));

	int local_serverfd = unix_domain_server_init(reader_proxy_path);
	
    pthread_t thread_tab[LANE_MAX_NUM][MAX_CAR_PER_LANE];
//	pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*reader_num);
    int *arg_buf = (int *)arg;

    int *pos_buf = (int *)malloc(sizeof(int) * g_lane_num * g_car_num_per_lane * 2);
    int index = 0;

    for(int i = 0; i < g_lane_num; i++){
        for(int j = 0; j < g_car_num_per_lane; j++){            
            pos_buf[index++] = arg_buf[i*2] - g_car_dist * j;
            pos_buf[index++] = 5*i + 3;
            LOG(INFO, "x:%d, y:%d", pos_buf[index-2], pos_buf[index-1]);
            pthread_create(&thread_tab[i][j], NULL, reader_thread, &pos_buf[index-2]);
        }   
    }

    int remote_clientfd = unix_domain_client_init(simulator_server_path);

    for(int i = 0; i < reader_num; i++){
        reader_conn[i] = accept(local_serverfd, NULL, NULL);
        printf("accept:%d\n", reader_conn[i]);
    }

    free(pos_buf);

    printf("init ok\n");

    int ready_num = 0;
    int sent_num = 0;

    req_bat_t *ready_req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
    req_bat_t *sent_req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
    rsp_bat_t *rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));

    trigger_reader(reader_conn);
    LOG(INFO, "TRIGGER OK");
    while(1){
        fd_set fds;
        FD_ZERO(&fds);
        int maxfd = -1;
//        FD_SET(trace_fd, &fds);
//        int maxfd = trace_fd;
        for(int i = 0; i < reader_num; i++){
            FD_SET(reader_conn[i], &fds);
            maxfd = maxfd > reader_conn[i] ? maxfd : reader_conn[i];
        }
        int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if(ret > 0){
//            if(FD_ISSET(trace_fd, &fds)){   //receive from trace server
            if(0){   //receive from trace server
                char buf[64];
                int buflen = 0;
                buflen = read(trace_fd, buf, 64);
                if(buflen >= 5 && (memcmp(buf, "RESET", 5) == 0)){
                    //reset_flag = 1;
                    for(int i = 0; i < reader_num; i++){
                        printf("[%s]--RESET\n", __func__);
                        write(reader_conn[i], "RESET", strlen("RESET"));
                    }
                    break;
                }else if(buflen >= 4 && (memcmp(buf, "KILL", 4) == 0)){
                    //kill_flag = 1;
                    for(int i = 0; i < reader_num; i++){
                        write(reader_conn[i], "KILL", strlen("KILL"));
                    }
                    break;
                }
            }

            {   //receive from reader.
                for(int i = 0; i < reader_num; i++){                    
                    if(FD_ISSET(reader_conn[i], &fds)){
                        reader_request_t tmp_req;
                        int readlen = read(reader_conn[i], (char *)&tmp_req, sizeof(reader_request_t));
                        for(int j = 0; j < ready_req_bat->num; j++){
                            if(tmp_req.addr == ready_req_bat->req[j].addr){
                                memcpy(&ready_req_bat->req[j], &tmp_req, sizeof(reader_request_t));
                                ready_req_bat->req[j].conn = reader_conn[i];
                                continue;
                            }
                        }
                        memcpy(&ready_req_bat->req[ready_req_bat->num], &tmp_req, sizeof(reader_request_t));
                        ready_req_bat->req[ready_req_bat->num].conn = reader_conn[i];
                        ready_req_bat->num++;
                    }
                }
            }
            if(ready_req_bat->num != reader_num){
                continue;
            }

            select_reader(ready_req_bat, sent_req_bat);

//            vector<Point> lights;

            //send to tag proxy 
            write(remote_clientfd, (char *)sent_req_bat, sizeof(req_bat_t));
            LOG(INFO, "sent to tag proxy, req_num:%d", sent_req_bat->num);
            for(int i = 0; i < sent_req_bat->num; i++){
//                int x, y;
//                get_send_pos(&sent_req_bat->req[i], &x, &y);
//                lights.push_back(Point(x, y));
                printf("[%s]--send to tag_proxy--start_time:%d>>>>>", __func__, sent_req_bat->req[i].start_time);
                for(int j = 0; j < sent_req_bat->req[i].plen; j++){
                    printf("%02x ", sent_req_bat->req[i].payload[j]);
                }
                printf("\n");
            }

            //receive from tag proxy
            int readlen = read(remote_clientfd, (char *)rsp_bat, sizeof(rsp_bat_t));
            LOG(INFO, "receive from tag proxy, rsp_num:%d", rsp_bat->num);
            for(int i = 0; i < rsp_bat->num; i++){
                printf("[%s]--receive from tag_proxy--start_time:%d<<<<<", __func__, rsp_bat->rsp[i].start_time);
                for(int j = 0; j < rsp_bat->rsp[i].plen; j++){
                    printf("%02x ", rsp_bat->rsp[i].payload[j]);
                }
                printf("\n");
            }
            do_handler(sent_req_bat, ready_req_bat, rsp_bat);
        }
    }
    for(int i = 0; i < reader_num; i++){
        close(reader_conn[i]); 
    }

    for(int i = 0; i < g_lane_num; i++){
        for(int j = 0; j < g_car_num_per_lane; j++){
            pthread_join(thread_tab[i][j], NULL);
        }
    }   
    free(thread_tab);

    close(local_serverfd);
    close(remote_clientfd);

}



void usage(char *prog){
    printf("Usage:%s <lane_num> <velocity>\n", prog);
}

//reader_num<int>
//tag_num<int>
//lane_num<int>
//velocity<float>
//reader_position<(double x, double y)>

int main(int argc, char *argv[]){
    int *pos_buf = NULL;
    if(argc == 3){ 
//        reader_num = atoi(argv[1]);
//        tag_num = atoi(argv[1]);
        g_lane_num = atoi(argv[1]);
        g_velocity = atof(argv[2])/3600;    

        printf("g_velocity:%f\n", g_velocity);

        g_com_dist = 100.0;
        g_sys_fov = 20.0;

        g_car_num_per_lane = 5;
        g_car_dist = 50;

        reader_num = g_car_num_per_lane * g_lane_num;

        pos_buf = (int *)malloc(g_lane_num * 2 * sizeof(int));
        printf("pos\n");
        for(int i = 0; i < g_lane_num; i++){
            //random_x = get_random()%50;
            //pos_buf[i*2] = -2*i;
            pos_buf[i*2] = 0;
            pos_buf[i*2+1] = 0;
            printf("%d, %d\n", pos_buf[i*2], pos_buf[i*2+1]);
        }
    }else{
        usage(argv[0]);
        return 0;
    }   

    init_random();

    pthread_mutex_init(&reader_mutex, 0);

    pthread_mutex_init(&g_write_log_mutex, 0);

    char log_path[64];

    time_t t = time(0);    
    struct tm ttt = *localtime(&t);
    sprintf(log_path, "result-%4d-%02d-%02d_%02d_%02d_%02d_%dlane_%dvelocity.log", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour, ttt.tm_min, ttt.tm_sec, g_lane_num, (int)g_velocity);
    g_log_file_fd = open(log_path, O_RDWR|O_CREAT, 0664);

    trace_fd = unix_domain_client_init(trace_sock_path);

    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, reader_proxy, (void *)pos_buf);

/* 
    pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*reader_num);
    for(int i = 0; i < reader_num; i++){
        pthread_create(&thread_tab[i], NULL, reader_thread, NULL);
    }
*/
    pthread_join(thread_proxy, NULL);

    close(g_log_file_fd);
/*    
    for(int i = 0; i < reader_num; i++){
        pthread_join(thread_tab[i], NULL);
    }
    free(thread_tab);
*/    
    destroy_random();
}
