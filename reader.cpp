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
#include "lambertian.h"


#ifdef LOG_LEVEL   
#undef LOG_LEVEL
//#define LOG_LEVEL 100
#define LOG_LEVEL FATAL
#endif


using namespace std;


int g_receive_item_count = 0;
#define LOG_FETCH_ITEM  50


#define PI 3.141592654

double velocity_dist_tbl[VELOCITY_NUM][2]={
    {48.28032, 22.86},   //30mph, 75feet
    {80.4672, 53.34},
    {112.65408, 96.012},
};




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
    int time;
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
    int visited;
    int res;
}rsp_ind_t;

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
    double posx;
    double posy;
    double velocity;
    int carrier_block_time;
    char last_collision_num;
    char last_query_addr;
    char last_round;
    char last_pair_num;
    addr_pair_t last_pair[TAG_MAX_NUM];
    char received_id[TAG_MAX_NUM];
    char received_num;
    int die_flag;
    int uuid;
    int recorded[TAG_MAX_NUM];
//    rsp_ind_t rsp_ind[TAG_MAX_NUM];
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


double g_tag_pos[TAG_MAX_NUM] = {0.0};

pthread_mutex_t reader_mutex;

pthread_mutex_t reader_finish_mutex;

pthread_mutex_t g_write_log_mutex;

pthread_mutex_t g_uuid_mutex;

int g_uuid = 0;

int g_reader_finish_num = 0;

uint64_t g_elapsed_time = 0;

int reader_num;
int tag_num;
int g_velocity_type;
int g_lane_num;
int g_tag_spacing;
double g_velocity;         //unit--m/ms



//double g_com_dist_up;
double g_com_dist_up[SIGN_TYPE_MAX_NUM+1];
double g_com_dist_down;
double g_sys_fov;           //unit--rad


int g_car_num_per_lane;
int g_car_dist;


reader_t reader_item_table[READER_MAX_NUM];

int trace_fd = -1;

char trace_buf[512] = {0};

const char *trace_sock_path = "trace.sock";
const char *reader_proxy_path = "reader_proxy.sock";
const char *simulator_server_path = "server.socket";

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

req_bat_t *select_reader(req_bat_t *ready_req_bat, req_bat_t *sent_req_bat){
    req_bat_t *to_tag_proxy_req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
    memset(to_tag_proxy_req_bat, 0, sizeof(req_bat_t));

    sort(ready_req_bat->req, ready_req_bat->req + reader_num, reader_request_cmp_descent);
    for(int i = reader_num - 1; i >= 0; i--){
        if(ready_req_bat->req[reader_num - 1].start_time + (ready_req_bat->req[reader_num - 1].plen << 3)*1000/DOWNLINK_BITRATE < ready_req_bat->req[i].start_time){
            memcpy(&to_tag_proxy_req_bat->req[to_tag_proxy_req_bat->num], &ready_req_bat->req[i], sizeof(reader_request_t));
            to_tag_proxy_req_bat->req[to_tag_proxy_req_bat->num++].type = NOT_REAL_SENT;
//            break;
        }else{            
            memcpy(&sent_req_bat->req[sent_req_bat->num++], &ready_req_bat->req[i], sizeof(reader_request_t));
            memcpy(&to_tag_proxy_req_bat->req[to_tag_proxy_req_bat->num], &ready_req_bat->req[i], sizeof(reader_request_t));
            to_tag_proxy_req_bat->req[to_tag_proxy_req_bat->num++].type = REAL_SENT;
        }
    }
    ready_req_bat->num = reader_num - sent_req_bat->num;
    return to_tag_proxy_req_bat;
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
    LOG(CRIT, "uplink collision happen, collision number:%d", reader->collision_num);
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

int lookup_pair(reader_t *reader, char reader_addr, char tag_addr, char tag_round){
    for(int i = 0; i < reader->pair_num; i++){
        if(reader->start_time - reader->pair[i].time > g_com_dist_down/reader->velocity){
            reader->pair_num--;
            for(int j = i; j < reader->pair_num; j++){
               memcpy(&reader->pair[j], &reader->pair[j+1], sizeof(addr_pair_t));
            }
        }
    }
    
    for(int i = 0; i < reader->pair_num; i++){
        if(reader->pair[i].reader == reader_addr && reader->pair[i].tag == tag_addr && reader->pair[i].round == tag_round){
            return 1;
        }
    }
    return 0;
}

void add_pair(reader_t *reader, char reader_addr, char tag_addr, char tag_round, int time){
    reader->pair[reader->pair_num].reader = reader_addr;
    reader->pair[reader->pair_num].tag = tag_addr;
    reader->pair[reader->pair_num].round = tag_round;
    reader->pair[reader->pair_num].time = time;
    reader->pair_num++;
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


void record_log(reader_t *reader, tag_response_t *tag_response){
    //double com_x_left = sqrt(pow(g_com_dist_up, 2) - pow(reader->posy,2));
#if 0
    double com_x_left = sqrt(pow(g_com_dist_up[tag_response->sign_type], 2) - pow(reader->posy,2));
    double com_x_right = reader->posy / tan(g_sys_fov);
#else
    double com_x_left, com_x_right;
    if(get_xaxis_range(reader->posy, tag_response->posx, tag_response->posy, g_com_dist_up[tag_response->sign_type], com_x_left, com_x_right) < 0){
/*
        while(1){
            printf("[%s]---ERROR HERE!\n", __func__);
        }
*/        
    }
    printf("l:%f, r:%f\n", com_x_left, com_x_right);
#endif
    int delta_t = tag_response->start_time + (tag_response->plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME - reader->init_time;
    double x = reader->posx + delta_t * reader->velocity;
    char log_s[512];
    char res_s[512];
    time_t t = time(0);
    struct tm ttt = *localtime(&t);
    RECORD_TRACE(g_log_file_fd, log_s, "[%4d-%02d-%02d %02d:%02d:%02d] time:%d--tag[%d](%f, %f) ---> reader[%d](%f, %f). comm_dist:%fm, remain_dist:%fm, com_x_left:%f, com_x_right:%f\n", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour,ttt.tm_min, ttt.tm_sec,
            delta_t + reader->init_time, get_tag_uuid(tag_response), tag_response->posx, tag_response->posy, reader->addr, x, reader->posy, com_x_left-com_x_right, tag_response->posx - x - com_x_right, com_x_left, com_x_right);
    //RECORD_TRACE(g_res_file_fd, res_s, "%f, %f, %f\n", reader->posy, com_x_left-com_x_right, tag_response->posx - x - com_x_right);
    //RECORD_TRACE(g_res_file_fd, res_s, "reader_uuid:%d, reader(%f,%f), tag(%f,%f)\n", reader->uuid, x, reader->posy, tag_response->posx, tag_response->posy);
//    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f\n", reader->uuid, x, reader->posy, tag_response->posx, tag_response->posy);
//    double l,r;    
//    get_xaxis_range(reader->posy, tag_response->posx, tag_response->posy, g_com_dist_up[tag_response->sign_type], l, r);
#if 0
    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f,%f,%f\n", reader->uuid, x, reader->posy, tag_response->posx, tag_response->posy, com_x_left-com_x_right, tag_response->posx - x - com_x_right);
#else    
    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f,%f,%f\n", reader->uuid, x, reader->posy, tag_response->posx, tag_response->posy, com_x_right-com_x_left, com_x_right - x);
#endif    
    //reader_uuid, reader_posx, reader_posy, tag_posx, tag_posy, comm_dist, remain_dist

    
    for(int i = 0; i < TAG_MAX_NUM; i++){
        if(abs(tag_response->posx - g_tag_pos[i]) < 1e-8){
            reader->recorded[i] = 1;
        }else if(abs(g_tag_pos[i]) < 1e-8){
            break;
        }
    }
//    reader->recorded[(int)((tag_response->posx-TAG_SPACING_OFFSET)/g_tag_spacing)] = 1;

    if(g_receive_item_count > LOG_FETCH_ITEM){
        reader->die_flag = 1;
    }
    g_receive_item_count++;
}

void uplink_data_handler(reader_t *reader, tag_response_t *tag_response){
    pthread_mutex_lock(&g_write_log_mutex);
    if(updata_receive_id_table(reader, tag_response) == 0){
        record_log(reader, tag_response);    
    }
    pthread_mutex_unlock(&g_write_log_mutex);

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
    return a.start_time + (a.plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME < b.start_time + (b.plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME; 
}

int sof_in_range(reader_t *reader, tag_response_t *tag_response){
//start of frame
    double start_posx = reader->posx + (tag_response->start_time - reader->init_time) * reader->velocity;
    double start_delta_x = start_posx - tag_response->posx;
    double start_delta_y = reader->posy - tag_response->posy;
    double start_distance = sqrt(pow(start_delta_x, 2) + pow(start_delta_y, 2));
    double start_degree = atan(fabs(start_delta_y)/fabs(start_delta_x));
#if 1 
    if(is_connected(start_distance, cos(start_degree), g_com_dist_up[tag_response->sign_type], g_sys_fov)){
        return 1;
    }else{
        return 0;
    }
#else    
    //if(start_distance < g_com_dist_up && start_degree < g_sys_fov && start_delta_x < 0){
    if(start_distance < g_com_dist_up[tag_response->sign_type] && start_degree < g_sys_fov && start_delta_x < 0){
        return 1;
    }else{
        LOG(INFO, "start_distance:%f, start_degree:%f, reader(%f, %f), tag(%f, %f), tag_rsp_start_time:%d", start_distance, start_degree, start_posx, reader->posy, tag_response->posx, tag_response->posy, tag_response->start_time);
        return 0;
    }
#endif    
}

void piggyback_data_handler(reader_t *reader, tag_response_t *tag_response_tbl, int rsp_num){       //not for me
    if(rsp_num == 1){
        if(sof_in_range(reader, &tag_response_tbl[0])){
            if(lookup_pair(reader, tag_response_tbl[0].payload[0], tag_response_tbl[0].payload[1] >> 4, tag_response_tbl[0].payload[2]) == 0){
                add_pair(reader, tag_response_tbl[0].payload[0], tag_response_tbl[0].payload[1] >> 4, tag_response_tbl[0].payload[2], tag_response_tbl[0].start_time);
                pthread_mutex_lock(&g_write_log_mutex);
                if(updata_receive_id_table(reader, &tag_response_tbl[0]) == 0){
                    record_log(reader, &tag_response_tbl[0]);
                }
                pthread_mutex_unlock(&g_write_log_mutex);
            }
        }
    }else if(rsp_num == 2){
        sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_cmp_ascent);
        if(((tag_response_tbl[0].type == DISC_ACK) && (tag_response_tbl[1].type == DATA))){     //piggyback
            if(lookup_pair(reader, tag_response_tbl[1].payload[0], tag_response_tbl[1].payload[1] >> 4, tag_response_tbl[0].payload[2]) == 0){
                add_pair(reader, tag_response_tbl[1].payload[0], tag_response_tbl[1].payload[1] >> 4, tag_response_tbl[1].payload[2], tag_response_tbl[1].start_time);
                pthread_mutex_lock(&g_write_log_mutex);                
                if(updata_receive_id_table(reader, &tag_response_tbl[1]) == 0){
                    record_log(reader, &tag_response_tbl[1]);
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
    reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME;
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


void reader_init(reader_t *reader, int conn, double *arg_buf){
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
    memset(reader->received_id, 0, sizeof(reader->received_id));
    reader->received_num = 0;
    reader->die_flag = 0;
    
    memset(reader->recorded, 0, sizeof(reader->recorded));
    pthread_mutex_lock(&g_uuid_mutex);
#if 0
    char res_s[256];
    memset(res_s, 0, sizeof(res_s));
    RECORD_TRACE(g_res_file_fd, res_s, "reader_uuid:%d, died\n", reader->uuid);
#endif    
    g_uuid++;
    reader->uuid = g_uuid;
    pthread_mutex_unlock(&g_uuid_mutex);
//    memset(reader->rsp_ind, 0, sizeof(reader->rsp_ind));
}

int enframe(reader_t *reader, char *txbuf){
    int txbuflen = 0;
    txbuf[0] = reader->addr;
    txbuf[1] = ((reader->state + reader->ack) << 4);
    txbuf[1] += reader->query_addr;
    if(reader->state == DISCOVERY_REQUEST){
        txbuf[2] = reader->round;
        txbuf[3] = (reader->collision_num << 4);
        txbuf[3] += reader->pair_num;        
        for(int i = 0; i < reader->pair_num; i++){
            txbuf[i*3+4] = reader->pair[i].reader;
            txbuf[i*3+5] = reader->pair[i].tag;
            txbuf[i*3+6] = reader->pair[i].round;
        }
        txbuflen = 4 + reader->pair_num * 3;
        txbuf[txbuflen++] = 0;
    }else{
        txbuf[2] = reader->round;
        txbuf[3] = 0;
        txbuflen = 4;
    }
    return txbuflen;
}

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




int in_range(reader_request_t *reader_request, tag_response_t *tag_response){
//    return 1;
    //distance
    //fov
    //blockage
    

//end of frame
    double cur_posx = reader_request->posx + (tag_response->start_time + (tag_response->plen << 3)*1000/UPLINK_BITRATE + PREAMBLE_TIME - reader_request->init_time) * reader_request->velocity;
    double delta_x = cur_posx  - tag_response->posx;
    if(delta_x > 0){
        return 0;
    }
    double delta_y = reader_request->posy - tag_response->posy;
    double distance = sqrt(pow(delta_x, 2) + pow(delta_y, 2));
    double degree = atan(fabs(delta_y)/fabs(delta_x));
    
#if 1 
    if(is_connected(distance, cos(degree), g_com_dist_up[tag_response->sign_type], g_sys_fov)){
        return 1;
    }else{
        return 0;
    }
    
#else
    //if(distance < g_com_dist_up && degree < g_sys_fov && delta_x < 0){
    if(distance < g_com_dist_up[tag_response->sign_type] && degree < g_sys_fov && delta_x < 0){
        if(be_blocked(reader_request, tag_response)){
            return 0;
        }else{
//            LOG(DEBUG, "rsp_start_time:%d, tag[%d](%d, %d)-->reader[%d](%d,%d), distance:%lf, degree:%lf, g_com_dist:%lf, g_sys_fov:%lf", tag_response->start_time, get_tag_uuid(tag_response), tag_response->posx, tag_response->posy,
//                    reader_request->addr,cur_posx, reader_request->posy,distance,degree, g_com_dist_up, g_sys_fov);
            return 1;
        }
    }else{
        return 0;
    }
#endif    
}

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
        reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME;
        uplink_collision_handler(reader); 
    }else if(rsp_num == 2){
            if(((tag_response_tbl[0].type == DISC_ACK) && (tag_response_tbl[1].type == DATA))){
                //UPLINK DATA
                reader->carrier_block_time = tag_response_tbl[1].start_time + (tag_response_tbl[1].plen<<3)*1000/UPLINK_BITRATE + PREAMBLE_TIME;
                if(get_dst_addr(&tag_response_tbl[1]) == reader->addr){
                    uplink_ack_handler(reader);
                    uplink_data_handler(reader, &tag_response_tbl[1]);
                }else{
                    piggyback_data_handler(reader, &tag_response_tbl[1], 1);
                }
            }else{
                //UPLINK COLLISION
                sort(tag_response_tbl, tag_response_tbl + rsp_num, tag_response_end_cmp_ascent);
                reader->carrier_block_time = tag_response_tbl[rsp_num - 1].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME;
                uplink_collision_handler(reader);
            }
    }else{
        reader->carrier_block_time = tag_response_tbl[0].start_time + (tag_response_tbl[rsp_num - 1].plen << 3) * 1000/UPLINK_BITRATE + PREAMBLE_TIME;
        if(sof_in_range(reader, &tag_response_tbl[0])){
            //        LOG(INFO, "carrier_block_time:%d, tag_response_tbl[0].start_time:%d, tag_response_tbl[rsp_num - 1].plen:%d", reader->carrier_block_time, tag_response_tbl[0].start_time, tag_response_tbl[rsp_num - 1].plen);
            if(get_dst_addr(&tag_response_tbl[0]) == reader->addr){
                uplink_data_handler(reader, &tag_response_tbl[0]);
            }else{
                piggyback_data_handler(reader, &tag_response_tbl[0], 1);
            }
        }else{
            LOG(INFO, "SOF is not in range, reader[%d]--response_starttime:%d", reader->addr, tag_response_tbl[0].start_time);
            uplink_collision_handler(reader);
        }
    }
}

reader_request_t *gen_reader_request(reader_t *reader){
    reader_request_t *reader_request = (reader_request_t *)malloc(sizeof(reader_request_t));
    if(!reader_request){
        LOG(FATAL, "MEM ERR");
//        while(1);
    }
    reader_request->conn = -1;
    reader_request->addr = reader->addr;
    reader_request->posx = reader->posx;
    reader_request->posy = reader->posy;
    reader_request->velocity = reader->velocity;
    reader_request->start_time = reader->start_time;
    reader_request->init_time = reader->init_time;
    reader_request->plen = enframe(reader, reader_request->payload) + (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT);

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


//#define RIGHT_TAG TAG_MAX_NUM

//init_posx
//init_posy
void send_reader_request_to_reader_proxy(reader_t *reader){
    double cur_posx = reader->posx + (reader->start_time - reader->init_time) * reader->velocity;
    if( cur_posx > (TAG_AXIS_NUM*g_tag_spacing)){    
        char res_s[256];
        memset(res_s, 0, sizeof(res_s));
//        RECORD_TRACE(g_res_file_fd, res_s, "%d,-1,-1,-1,-1\n", reader->uuid);

        int start_time;
        double pos_buf[2];        
        start_time = reader->start_time;
        pos_buf[0] = reader->posx + (reader->start_time - reader->init_time) * reader->velocity - g_car_num_per_lane * g_car_dist;
        pos_buf[1] = reader->posy;
        reader_init(reader, reader->conn, pos_buf);
        reader->init_time = start_time;
        LOG(INFO, "new reader--->(%f, %f)--init_time:%d", reader->posx, reader->posy, reader->init_time);
    }
//    LOG(INFO, "[%d]---collision_num:%d", reader->conn, reader->collision_num);
    reader_request_t *reader_request = NULL;
    reader_request = gen_reader_request(reader);    
    if(!reader_request){
        LOG(FATAL, "MEM ERR!");
//        while(1);
    }
   // LOG(INFO, "[%d]---velocity:%f, start_time:%d", reader->conn, reader_request->velocity, reader_request->start_time);
    int sendlen = write(reader->conn, (char *)reader_request, sizeof(reader_request_t));
    if(reader->state == DISCOVERY_REQUEST){
        reader_backup(reader);

        reader->query_addr = 0;
        reader->collision_num = 0;
//        reader->round;
        reader->pair_num = 0;
        memset(reader->pair, 0, sizeof(addr_pair_t));
    }
    //LOG(INFO, "reader[%d] start_time:%d", reader->addr, reader->start_time);
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

    double *arg_buf = (double *)arg;
    reader_init(reader, conn, arg_buf);

    printf("conn:%d, (%f, %f)\n", conn, reader->posx, reader->posy);

    char log_str[2048] = {0};
    char tmp_str[512] = {0};

    reader_request_t *reader_request = (reader_request_t *)malloc(sizeof(reader_request_t));
    memset(reader_request, 0, sizeof(reader_request_t));

    tag_response_t *tag_response_tbl[TAG_MAX_NUM] = {NULL};
    rsp_bat_t *tag_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
    while(1){
        if(reader->die_flag){
#if 0        
            pthread_mutex_lock(&g_write_log_mutex);
            double cur_posx = reader->posx + (reader->start_time - reader->init_time) * reader->velocity;
            char res_s[256];
            memset(res_s, 0, sizeof(res_s));
            RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,-2,-2\n", reader->uuid, cur_posx, reader->posy);   
            pthread_mutex_unlock(&g_write_log_mutex);
            printf("died\n");
#endif            
            break;
        }
        int rsp_num = 0;
        
        memset(tag_rsp_bat, 0, sizeof(rsp_bat_t));

        read(conn, (char *)tag_rsp_bat, sizeof(rsp_bat_t));

        pthread_mutex_lock(&reader_mutex);

        double cur_posx = reader->posx + (reader->start_time - reader->init_time) * reader->velocity;
        char res_s[256];
        memset(res_s, 0, sizeof(res_s));


        for(int i = 0; i < TAG_MAX_NUM; i++){
            if((abs(g_tag_pos[i]) > 1e-8) && cur_posx > g_tag_pos[i]){
                if(reader->recorded[i] == 0){
                    //RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f,%f,%f\n", reader->uuid, cur_posx, reader->posy, (double)(TAG_SPACING_OFFSET + (tag_num-1)*g_tag_spacing), 0.0, 0.0, 0.0);   
                    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f,%f,%f\n", reader->uuid, cur_posx, reader->posy, g_tag_pos[i], 0.0, 0.0, 0.0);   
                    reader->recorded[i] = 1;
                    g_receive_item_count++;
                }
            }else if(abs(g_tag_pos[i]) < 1e-8){
                break;
            }
        }

/* 
        for(int i = 0; i < tag_num; i++){
            for(int i = 0; i < TAG_MAX_NUM; i++){
                if(abs(tag_response->posx - g_tag_pos[i]) < 1e-8){
                    reader->recorded[i] = 1;
                }else if(abs(g_tag_pos[i]) < 1e-8){
                    break;
                }
            }
            //if((cur_posx - TAG_SPACING_OFFSET)/(double)g_tag_spacing > tag_num){
            if(){
                if(reader->recorded[tag_num-1] == 0){
                    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f\n", reader->uuid, cur_posx, reader->posy, (double)(TAG_SPACING_OFFSET + (tag_num-1)*g_tag_spacing), 0.0);   
                    g_receive_item_count++;
                    reader->recorded[tag_num-1] = 1;
                }
            }else if(((cur_posx - TAG_SPACING_OFFSET)/(double)g_tag_spacing > i) && ((cur_posx - TAG_SPACING_OFFSET)/g_tag_spacing < i + 1)){
                if(reader->recorded[i-1] == 0){
                    RECORD_TRACE(g_res_file_fd, res_s, "%d,%f,%f,%f,%f\n", reader->uuid, cur_posx, reader->posy, (double)(TAG_SPACING_OFFSET + (i-1)*g_tag_spacing), 0.0);   
                    g_receive_item_count++;
                    reader->recorded[i-1] = 1;
                }
            }
        }
*/

        if(tag_rsp_bat->type == INIT_TRIGGER){
            send_reader_request_to_reader_proxy(reader);
        }else{
            if(tag_rsp_bat->sent){  //maybe for me
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
                    handler_piggyback_response(tag_rsp_bat->rsp, reader, tag_rsp_bat->num);         //UPLINK DATA or UPLINK COLLISION or DISC_ACK
                }
                send_reader_request_to_reader_proxy(reader);
            }
        }
        pthread_mutex_unlock(&reader_mutex);
    }
    printf(" thread finish\n");
    close(conn);
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


void do_handler(req_bat_t *sent_req_bat, 
                req_bat_t *ready_req_bat, 
                rsp_bat_t *rsp_bat){ 

    for(int i = 0; i < sent_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = NULL;
        to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        if(!to_reader_rsp_bat){
            LOG(FATAL, "MEM ERR");
//            while(1);
        }
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 1;
        to_reader_rsp_bat->type = TAG_RSP;

        for(int j = 0; j < rsp_bat->num; j++){      //
            vector<Point> lights;
            for(int k = 0; k < sent_req_bat->num; k++){
                double x,y;
                get_recv_pos(&sent_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            for(int k = 0; k < ready_req_bat->num; k++){
                double x,y;
                get_recv_pos(&ready_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }

            double x,y;
            get_recv_pos(&sent_req_bat->req[i], &rsp_bat->rsp[j], &x, &y);                
            lights.push_back(Point(x, y)); 

            char p_str[64] = {0};
            memset(p_str, 0, sizeof(p_str));
            for(int k = 0; k < rsp_bat->rsp[j].plen - (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT); k++){
                sprintf(p_str, "%s %02x", p_str, rsp_bat->rsp[j].payload[k]);
            }

            if(in_range(&sent_req_bat->req[i], &rsp_bat->rsp[j])){                
                if(!is_intersect(lights, Point(rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy), lights.size() - 1)){
                    LOG(INFO, "sent-reader[%3d]  is [in-view ]. reader(%3f, %3f)<----tag(%3f, %3f), rsp_start_time:%10d, receive data:%s", sent_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);                
                    memcpy((char *)&(to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++]), (char *)&(rsp_bat->rsp[j]), sizeof(tag_response_t)); 
                }else{
                    LOG(INFO, "sent-reader[%3d]  is [in-view but intersect]. reader(%3f, %3f)<----tag(%3f, %3f), rsp_start_time:%10d, receive data:%s", sent_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);                
#if 0                    
                    while(lights.size()){
                        Point a = lights.back();
                        lights.pop_back();
                        printf("(%f, %f)\n", a.x, a.y);
                    }
#endif                    
                }
            }else{
//                LOG(INFO, "sent-reader[%3d]  is [out-view]. reader(%3d, %3d)<----tag(%3d, %3d), rsp_start_time:%10d, receive data:%s", sent_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);
            }
        }

        write(sent_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }

    for(int i = 0; i < ready_req_bat->num; i++){
        rsp_bat_t *to_reader_rsp_bat = NULL;
        to_reader_rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        if(!to_reader_rsp_bat){
            LOG(FATAL, "MEM ERR");
//            while(1);
        }
        to_reader_rsp_bat->num = 0;
        to_reader_rsp_bat->sent = 0;
        to_reader_rsp_bat->type = TAG_RSP;

        for(int j = 0; j < rsp_bat->num; j++){
            vector<Point> lights;
            for(int k = 0; k < sent_req_bat->num; k++){
                double x,y;
                get_recv_pos(&sent_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            for(int k = 0; k < ready_req_bat->num; k++){
                double x,y;
                get_recv_pos(&ready_req_bat->req[k], &rsp_bat->rsp[j], &x, &y);
                lights.push_back(Point(x, y));
            }
            double x,y;
            get_recv_pos(&ready_req_bat->req[i], &rsp_bat->rsp[j], &x, &y);
            lights.push_back(Point(x, y));


            char p_str[64] = {0};
            memset(p_str, 0, sizeof(p_str));
            for(int k = 0; k < rsp_bat->rsp[j].plen - (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT); k++){
                sprintf(p_str, "%s %02x", p_str, rsp_bat->rsp[j].payload[k]);
            }
            if(in_range(&ready_req_bat->req[i], &rsp_bat->rsp[j])){                
                if(!is_intersect(lights, Point(rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy), lights.size() - 1)){
                    LOG(INFO, "ready-reader[%3d] is [in-view ]. reader(%3f, %3f)<----tag(%3f, %3f), rsp_start_time:%10d, receive data:%s", ready_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);
                    memcpy((char *)&(to_reader_rsp_bat->rsp[to_reader_rsp_bat->num++]), (char *)&(rsp_bat->rsp[j]), sizeof(tag_response_t)); 
                }else{
                    LOG(INFO, "ready-reader[%3d] is [in-view but intersect]. reader(%3f, %3f)<----tag(%3f, %3f), rsp_start_time:%10d, receive data:%s", ready_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);
#if 0                    
                    while(lights.size()){
                        Point a = lights.back();
                        lights.pop_back();
                        printf("(%f, %f)\n", a.x, a.y);
                    }
#endif                    
                }
            }else{
//                LOG(INFO, "ready-reader[%3d] is [out-view]. reader(%3d, %3d)<----tag(%3d, %3d), rsp_start_time:%10d, receive data:%s", sent_req_bat->req[i].addr, x, y, rsp_bat->rsp[j].posx, rsp_bat->rsp[j].posy, rsp_bat->rsp[j].start_time, p_str);
            }
        }
        write(ready_req_bat->req[i].conn, (char *)to_reader_rsp_bat, sizeof(rsp_bat_t));
        free(to_reader_rsp_bat);
    }
    sent_req_bat->num = 0;
    ready_req_bat->num = 0;
}



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

    double pos_buf[MAX_LANE_NUM][VEHICLE_NUM_PER_LANE][2]; 

    for(int i = 0; i < g_lane_num; i++){
        for(int j = 0; j < g_car_num_per_lane; j++){            
            if(j == 0){
                pos_buf[i][j][0] = -(random()%(int)velocity_dist_tbl[g_velocity_type][1]);
                pos_buf[i][j][1] = TAG_POSY_OFFSET + LANE_WIDTH * (i + 0.5);
            }else{
                pos_buf[i][j][0] = pos_buf[i][j-1][0] - velocity_dist_tbl[g_velocity_type][1];
                pos_buf[i][j][1] = TAG_POSY_OFFSET + LANE_WIDTH * (i + 0.5);
            }
            printf("(%d,%d)==(%f, %f)\n", i, j, pos_buf[i][j][0], pos_buf[i][j][1]);
            pthread_create(&thread_tab[i][j], NULL, reader_thread, pos_buf[i][j]);
        }   
    }

    int remote_clientfd = unix_domain_client_init(simulator_server_path);

    memset(g_tag_pos, 0.0, sizeof(g_tag_pos));
    sign_info_t sign_info[TAG_MAX_NUM];
    //read(remote_clientfd, g_tag_pos, sizeof(g_tag_pos));
    read(remote_clientfd, sign_info, sizeof(sign_info));

    for(int i = 0; i < TAG_MAX_NUM; i++){
        printf("tag[%d]:(%f, %f)\n", i, sign_info[i].posx, 0.0);
        g_tag_pos[i] = sign_info[i].posx;
    }

    for(int i = 0; i < reader_num; i++){
        reader_conn[i] = accept(local_serverfd, NULL, NULL);
        printf("accept:%d\n", reader_conn[i]);
    }


    printf("init ok\n");

    int ready_num = 0;
    int sent_num = 0;

    req_bat_t *ready_req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
    req_bat_t *sent_req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
    rsp_bat_t *rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));

    trigger_reader(reader_conn);
    LOG(INFO, "TRIGGER OK");
    while(1){
        if(g_receive_item_count>LOG_FETCH_ITEM){
            break;
        }
        fd_set fds;
        FD_ZERO(&fds);
        int maxfd = -1;
        for(int i = 0; i < reader_num; i++){
            FD_SET(reader_conn[i], &fds);
            maxfd = maxfd > reader_conn[i] ? maxfd : reader_conn[i];
        }
        int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
        if(ret > 0){
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

            req_bat_t *to_tag_proxy_req_bat = select_reader(ready_req_bat, sent_req_bat);

            //send to tag proxy 
            //write(remote_clientfd, (char *)sent_req_bat, sizeof(req_bat_t));
            write(remote_clientfd, (char *)to_tag_proxy_req_bat, sizeof(req_bat_t));
            free(to_tag_proxy_req_bat);
            //receive from tag proxy
            int readlen = read(remote_clientfd, (char *)rsp_bat, sizeof(rsp_bat_t));

            do_handler(sent_req_bat, ready_req_bat, rsp_bat);
            usleep(10000);
        }
    }
    for(int i = 0; i < reader_num; i++){
        close(reader_conn[i]); 
    }
    for(int i = 0; i < g_lane_num; i++){
        for(int j = 0; j < g_car_num_per_lane; j++){
            printf("i,j :%d, %d\n", i, j);
            int rc = pthread_join(thread_tab[i][j], NULL);
            if(rc){
                printf("pthread join error\n");
                perror("ERR:");
            }else{
                printf("pthread join ok\n");
            }
        }
    }   
    close(g_res_file_fd);

    close(local_serverfd);
    close(remote_clientfd);

    free(ready_req_bat);
    free(sent_req_bat);
    free(rsp_bat);

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
    double *pos_buf = NULL;
    if(argc == 4){ 
        g_lane_num = atoi(argv[1]);
    
        g_velocity_type = atoi(argv[2]);

        g_tag_spacing = atoi(argv[3]);

        tag_num = TAG_ROAD_LENGTH/g_tag_spacing;

        //g_velocity = atof(argv[2])/3600;    
        g_velocity = velocity_dist_tbl[g_velocity_type][0]/3600;    
        g_car_dist = velocity_dist_tbl[g_velocity_type][1];

        printf("g_velocity:%f\n", g_velocity);

        g_com_dist_down = DOWNLINK_DISTANCE;
//        g_com_dist_up = UPLINK_DISTANCE;
        g_com_dist_up[SMALL_SIGN] = SMALL_SIGN_UPLINK_DISTANCE;
        g_com_dist_up[LARGE_SIGN] = LARGE_SIGN_UPLINK_DISTANCE;

        g_sys_fov = SYS_FOV/2*PI/180;

        g_car_num_per_lane = VEHICLE_NUM_PER_LANE;
//        g_car_num_per_lane = 10;
//        g_car_dist = 50;

        reader_num = g_car_num_per_lane * g_lane_num;

    }else{
        usage(argv[0]);
        return 0;
    }   

    init_random();

    pthread_mutex_init(&reader_mutex, 0);

    pthread_mutex_init(&g_write_log_mutex, 0);

    pthread_mutex_init(&g_uuid_mutex, 0);

    char log_path[64];
    char res_path[64];

    time_t t = time(0);    
    struct tm ttt = *localtime(&t);
    sprintf(log_path, "trace-%4d-%02d-%02d_%02d_%02d_%02d_random%d_bps%d_signtype%d_lane%d_velocity%d_spacing%d.log", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour, ttt.tm_min, ttt.tm_sec, 
    RANDOM_INSERT_TAG, UPLINK_BITRATE, DEFAULT_SIGN_TYPE, g_lane_num, g_velocity_type, g_tag_spacing);
    g_log_file_fd = open(log_path, O_RDWR|O_CREAT, 0664);

    sprintf(res_path, "result-%4d-%02d-%02d_%02d_%02d_%02d_random%d_bps%d_signtype%d_lane%d_velocity%d_spacing%d.csv", ttt.tm_year + 1900, ttt.tm_mon + 1, ttt.tm_mday, ttt.tm_hour, ttt.tm_min, ttt.tm_sec, 
    RANDOM_INSERT_TAG, UPLINK_BITRATE, DEFAULT_SIGN_TYPE, g_lane_num, g_velocity_type, g_tag_spacing);
    g_res_file_fd = open(res_path, O_RDWR|O_CREAT, 0664);

    trace_fd = unix_domain_client_init(trace_sock_path);

    printf("before create pthread\n");
    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, reader_proxy, NULL);
    printf("after create pthread\n");

    pthread_join(thread_proxy, NULL);
    printf("exit process\n");

    close(g_log_file_fd);
    destroy_random();
}
