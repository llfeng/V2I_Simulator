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
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include "common.h"
#include "log.h"
#include "is_available.h"

using namespace std;


#define PI 3.141592654

#ifdef LOG_LEVEL
#undef LOG_LEVEL
#define LOG_LEVEL   FATAL
//#define LOG_LEVEL   100
#endif

#define USE_ROUND_ADDR 1


//#define READER_MAX_NUM  10
//#define TAG_MAX_NUM  10

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
    char round;
    int vaild;
    int time;
}tag_alias_t;

typedef struct{
    char reader;
    int time;
}tag_silent_t;

typedef struct{
    int conn;
    char dst;
    char addr;
    char round;
//    uplink_frame_t uplink_frame;
//    char silent[READER_MAX_NUM];
    tag_silent_t silent[READER_MAX_NUM];
    char silent_num;
    char txbuflen;
    char txbuf[FRAME_MAX_LEN];
    char alias_num;
    tag_alias_t alias[READER_MAX_NUM];
    double posx;
    double posy;
    int start_time;
    int busy_time;
    int sign_type;
}tag_t;


typedef struct{
    int vaild;
    int conn;
    int plen;
    char payload[FRAME_MAX_LEN];
}tag_info_t;


pthread_mutex_t tag_mutex;

tag_t tag_item_table[TAG_MAX_NUM];

const char *trace_sock_path = "trace.sock";
const char *tag_proxy_path = "tag_proxy.sock";
const char *simulator_server_path = "server.socket";


int reader_num;
int tag_num;
double g_spacing;
double g_velocity;
double g_com_dist_down;
double g_sys_fov;

int trace_fd = -1;

#if USE_ROUND_ADDR
void update_alias(tag_t *tag, char reader_addr, char short_addr, char round, int time){
    int exist_flag = 0;
    for(int i = 0; i < tag->alias_num; i++){
        if(reader_addr == tag->alias[i].reader){
            tag->alias[i].tag = short_addr;
            tag->alias[i].round = round;
            tag->alias[i].time = time;
            exist_flag = 1;
            break;
        }
    }
    if(exist_flag == 0){
        tag->alias[tag->alias_num].reader = reader_addr;
        tag->alias[tag->alias_num].tag = short_addr;
        tag->alias[tag->alias_num].round = round;
        tag->alias[tag->alias_num].time = time;
        tag->alias_num++;
    }

    if(tag->alias_num > READER_MAX_NUM){
        printf("silent_table:\n");
        for(int i = 0; i < tag->silent_num; i++){
            printf("reader:%d, time:%d\n", tag->silent[i].reader, tag->silent[i].time);
        }
        printf("alias_table:\n");
        for(int i = 0; i < tag->alias_num; i++){
            printf("reader:%d, tag:%d, round:%d, vaild:%d, time:%d\n", tag->alias[i].reader, tag->alias[i].tag, tag->alias[i].round, tag->alias[i].vaild, tag->alias[i].time);
        }
    }
    printf("tag->alias_num:%d", tag->alias_num);
}
#else
void update_alias(tag_t *tag, char reader_addr, char short_addr){
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
#endif

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

#if USE_ROUND_ADDR
int lookup_alias(tag_t *tag, char reader_addr, char short_addr, char round, int acked){
    for(int i = 0; i < tag->alias_num; i++){
        if(round == tag->alias[i].round &&
        reader_addr == tag->alias[i].reader && 
        short_addr == tag->alias[i].tag){
            if(acked){
                if(tag->alias[i].vaild){
                    return 1;
                }else{
                    LOG(CRIT, "NO ACKED BY OWNER");
                }
            }else{
                return 1;
            }
        }
    }
    return 0;
}
#else
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
#endif

#if USE_ROUND_ADDR
void send_to_proxy(tag_t *tag){
    tag->txbuf[0] = tag->dst;
    tag->txbuf[1] = (tag->addr << 4);
    tag->txbuf[1] += 1;
    tag->txbuf[2] = tag->round;

    tag->txbuf[3] = tag->conn;
    tag->txbuf[4] = 0x00;    //fcs
    tag->txbuflen = 5;
    printf("[%ld----send: %02x %02x %02x %02x %02x---]\n", 
    time(NULL), tag->txbuf[0], tag->txbuf[1], tag->txbuf[2], tag->txbuf[3], tag->txbuf[4]);
    tag_response_t *rsp = (tag_response_t *)malloc(sizeof(tag_response_t));
    rsp->plen = tag->txbuflen;
    memcpy(rsp->payload, tag->txbuf, tag->txbuflen);
    rsp->plen += (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT);
    rsp->start_time = tag->start_time;
    rsp->posx = tag->posx;
    rsp->posy = tag->posy;
    rsp->type = DATA;
    rsp->sign_type = tag->sign_type;
    
    tag->busy_time = rsp->start_time + (rsp->plen << 3) * 1000/UPLINK_BITRATE;

    int sent_bytes = write(tag->conn, (char *)rsp, sizeof(tag_response_t));
    free(rsp);
}
#else
void send_to_proxy(tag_t *tag){
    tag->txbuf[0] = tag->dst;
    tag->txbuf[1] = (tag->addr << 4);
    tag->txbuf[1] += 1;
    tag->txbuf[2] = tag->conn;
    tag->txbuf[3] = 0x00;    //fcs
    tag->txbuflen = 4;
    printf("[%ld----send: %02x %02x %02x %02x---]\n", 
    time(NULL), tag->txbuf[0], tag->txbuf[1], tag->txbuf[2], tag->txbuf[3]);
    int sent_bytes = write(tag->conn, tag->txbuf, tag->txbuflen);
}
#endif


void keep_silent(tag_t *tag, int reader_addr, int time){
    int exist_flag = 0;
    for(int i = 0; i < tag->silent_num; i++){
        if(tag->silent[i].reader == reader_addr){            
            exist_flag = 1;
            break;
        }
    }
    
    if(exist_flag == 0){
        tag->silent[tag->silent_num].time = time;
        tag->silent[tag->silent_num++].reader = reader_addr;
    }
}

int is_silent(tag_t *tag, int reader_addr){
    for(int i = 0; i < tag->silent_num; i++){
        if(tag->silent[i].reader == reader_addr){
            return 1;
        }
    }
    return 0;
}

#define FADE_TIMEOUT    5000

void check_alias(tag_t *tag, int cur_time){
    for(int i = 0; i < tag->alias_num; i++){
        if(cur_time - tag->alias[i].time > FADE_TIMEOUT){
            tag->alias_num--;
            for(int j = i; j < tag->alias_num; j++){
                memcpy(&tag->alias[j], &tag->alias[j+1], sizeof(tag_alias_t));
            }
        }
    }
}

void check_silent(tag_t *tag, int cur_time){
    for(int i = 0; i < tag->silent_num; i++){
        if(cur_time - tag->silent[i].time > FADE_TIMEOUT){
            tag->silent_num--;
            for(int j = i; j < tag->silent_num; j++){
                memcpy(&tag->silent[j], &tag->silent[j+1], sizeof(tag_silent_t));
            }
        }
    }
}

void send_nop_to_proxy(tag_t *tag){
    tag_response_t *rsp = (tag_response_t *)malloc(sizeof(tag_response_t));
    rsp->type = UPLINK_CANNOT_OUT; 
    write(tag->conn, (char *)rsp, sizeof(tag_response_t));
    free(rsp);
}


void parse_downlink(tag_t *tag, reader_request_t *req){     //mac
    char *buf = req->payload;
    char reader_addr = buf[0];
    char frame_type = buf[1] >> 4;
    char frame_state = frame_type & 0x04;
    char tag_addr = buf[1] & 0x0F;
    char round = buf[2];



    tag->start_time = req->start_time + (req->plen << 3) * 1000/DOWNLINK_BITRATE;

    char ack_round = 0;
    switch(frame_type & 0x03){
        case ACK:
            if(frame_state == DISCOVERY_REQUEST){
                ack_round = round-1;
            }else if(frame_state == QUERY_REQUEST){
                ack_round = round;
            }else{
                printf("unknown frame_state!\n");
                while(1);
            }
            for(int i = 0; i < tag->alias_num; i++){
                if(tag->alias[i].round == ack_round &&
                tag->alias[i].reader == reader_addr && 
                tag->alias[i].tag == tag_addr - 1){ 
                    tag->alias[i].vaild = 1;
                    keep_silent(tag, reader_addr, tag->start_time);
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
        char plen = buf[3] & 0x0F;
        LOG(INFO, "%ld--recv:%02x %02x %02x %02x", time(NULL), buf[0], buf[1], buf[2], buf[3]);
#if 0        
        printf("start---silent tab[%d]\n", tag->silent_num);
        for(int i = 0; i < tag->silent_num; i++){
            printf("silent reader:%d, time:%d\n", tag->silent[i].reader, tag->silent[i].time);
        }
        printf("end---silent\n");
#endif
        for(int i = 0; i < plen; i += 3){
            //LOG(INFO, "%02x %02x ", buf[i+4], buf[i+5]);
            if(lookup_alias(tag, buf[i+4], buf[i+5], buf[i+6], 1) == 1){
                keep_silent(tag, reader_addr, tag->start_time);
            }
        }
        if(is_silent(tag, reader_addr) == 1){
            send_nop_to_proxy(tag);
            //do nothing
//            delete_alias(tag, reader_addr);
        //}else{
        }else if(tag->busy_time < tag->start_time){
            char addr_range = 0;
            char collision_num = buf[3] >> 4;
            if(collision_num == 0){
                addr_range = 1;
            }else{
                addr_range = 2 * collision_num;
            }
            char short_addr = get_random()%addr_range;            
            LOG(INFO, "short_addr:%d", short_addr);
            update_alias(tag, reader_addr, short_addr, round, tag->start_time);
            if(short_addr == 0){
                tag->addr = short_addr;
                tag->dst = reader_addr;
                tag->round = round;
                tag->start_time += ENERGY_CHECK_TIME;
                send_to_proxy(tag);
            }else{
                send_nop_to_proxy(tag);
            }
        }else{
            send_nop_to_proxy(tag);
        }
    }else{  //QUERY_REQUEST
        printf("[%ld--recv:%02x %02x]\n", time(NULL), buf[0], buf[1]);
        if((tag->busy_time < tag->start_time) && (lookup_alias(tag, reader_addr, tag_addr, round, 0) == 1)){
            tag->addr = tag_addr;
            tag->dst = reader_addr;
            tag->round = round;
            send_to_proxy(tag);
        }else{
            send_nop_to_proxy(tag);
        }
    }
}


tag_t *create_tag(){
    tag_t *tag = (tag_t *)malloc(sizeof(tag_t));
    memset(tag, 0, sizeof(tag_t));
    return tag;
}

void tag_init(tag_t *tag, int conn, double posx, double posy, int sign_type){
    tag->alias_num = 0;
    memset(tag->alias, 0, sizeof(tag->alias));

    tag->silent_num = 0; 
    memset(tag->silent, 0, sizeof(tag->silent));

    tag->txbuflen = 0;
    memset(tag->txbuf, 0, sizeof(tag->txbuf));

    tag->conn = conn;
    tag->posx = posx;
    tag->posy = posy;

    tag->sign_type = sign_type;
}


int be_blocked(reader_request_t *reader_request, tag_t *tag){
	return 0;
}

int in_range(reader_request_t *reader_request, tag_t *tag){
//    return 1;
    //reader_request->posx
    //reader_request->posy
    //reader_request->elapsed_time
    //tag->posx
    //tag->posy
    //double cur_posx = reader_request->posx + (reader_request->start_time - reader_request->init_time) * reader_request->velocity + (reader_request->plen<<3)*1000/DOWNLINK_BITRATE;
    double cur_posx = reader_request->posx + (reader_request->start_time - reader_request->init_time + (double)(reader_request->plen<<3)*1000/DOWNLINK_BITRATE) * reader_request->velocity;
//    LOG(INFO, "start_time:%d, init_time:%d, velocity:%f, reader[%d](%d, %d)-->tag[%d](%d, %d)",reader_request->start_time, reader_request->init_time,reader_request->velocity, 
//    reader_request->addr, cur_posx, reader_request->posy, tag->conn, tag->posx, tag->posy);
    //LOG(INFO, "reader(%d, %d)-->tag(%d, %d)", reader_request->posx, reader_request->posy, tag->posx, tag->posy);
//    double delta_x = (reader_request->posx + (reader_request->start_time - reader_request->init_time) * reader_request->velocity) - tag->posx;
    double delta_x = cur_posx - tag->posx;
    //double delta_x = (reader_request->posx + reader_request->start_time * reader_request->velocity) - tag->posx;
    double delta_y = reader_request->posy - tag->posy;
    double distance = sqrt(pow(delta_x, 2) + pow(delta_y, 2));
    double degree = atan(fabs(delta_y)/fabs(delta_x));
    if(distance < g_com_dist_down && degree < g_sys_fov && delta_x < 0){
        if(be_blocked(reader_request, tag)){
            return 0;
        }else{
            char p_str[512] = {0};
            memset(p_str, 0, sizeof(p_str));
            for(int k = 0; k < reader_request->plen - (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT); k++){
                sprintf(p_str, "%s %02x", p_str, reader_request->payload[k]);
            }
//            LOG(INFO, "req_start_time:%d, init_time:%d, velocity:%f, reader[%d](%d, %d)-->tag[%d](%d, %d), receive data:%s",reader_request->start_time, reader_request->init_time,reader_request->velocity, 
//                    reader_request->addr, cur_posx, reader_request->posy, tag->conn, tag->posx, tag->posy, p_str);
            return 1;
        }
    }else{
        return 0;
    }   
}

typedef struct{
    double posx;
    int type;
} sign_info_t;

void *tag_thread(void *arg){
	tag_t *tag = create_tag();
    int conn = unix_domain_client_init(tag_proxy_path);
    //int posx = *((int *)arg);
    double posx = ((sign_info_t *)arg)[0].posx;
    double posy = 0;
    int sign_type = ((sign_info_t *)arg)[0].type;
    tag_init(tag, conn, posx, posy, sign_type);
    reader_request_t *reader_request_tbl[READER_MAX_NUM] = {NULL};
    int in_range_req_num;
    while(1){
        in_range_req_num = 0;
        req_bat_t *req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
        read(conn, (char *)req_bat, sizeof(req_bat_t));
        if(memcmp((char *)req_bat, "RESET", strlen("RESET")) == 0){
            free(req_bat);
            printf("[%s]--RESET\n", __func__);
            tag_init(tag, conn, posx, posy, sign_type);
            continue;
        }else if(memcmp((char *)req_bat, "KILL", strlen("KILL")) == 0){
            free(req_bat);
            break;
        }else{

            pthread_mutex_lock(&tag_mutex);

/*
            vector<Point> lights;
            for(int k = 0; k < req_bat->num; k++){
                double x,y;
                get_send_pos(&req_bat->req[k], &x, &y);
                lights.push_back(Point(x, y));
            }
*/
            reader_request_t *in_range_req = NULL;
            for(int i = 0; i < req_bat->num; i++){
            
                check_alias(tag, req_bat->req[i].start_time);
                check_silent(tag, req_bat->req[i].start_time);

                vector<Point> lights;
                for(int k = 0; k < req_bat->num; k++){
                    double x,y;
                    calibrate_reader_pos(&req_bat->req[k], req_bat->req[i].start_time, &x, &y);
                    lights.push_back(Point(x, y));
                }

                double x,y;
                get_send_pos(&req_bat->req[i], &x, &y);
                lights.push_back(Point(x, y));
                if(tag->posx < 100){
                    printf("ERROR HERE!\n");
                    while(1);
                }
                if((req_bat->req[i].type == REAL_SENT) && in_range(&req_bat->req[i], tag)){
                    double cur_posx = req_bat->req[i].posx + (req_bat->req[i].start_time - req_bat->req[i].init_time + (double)(req_bat->req[i].plen<<3)*1000/DOWNLINK_BITRATE) * req_bat->req[i].velocity;
                    char p_str[512] = {0};
                    memset(p_str, 0, sizeof(p_str));
                    for(int k = 0; k < req_bat->req[i].plen - (READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT); k++){
                        sprintf(p_str, "%s %02x", p_str, req_bat->req[i].payload[k]);
                    }
                    if(!is_intersect(lights, Point(tag->posx, tag->posy), lights.size() - 1)){
                        LOG(INFO, "req_start_time:%d, [in-veiw ]init_time:%d, reader[%d](%f, %f)-->tag[%d](%f, %f), receive data:%s",req_bat->req[i].start_time, req_bat->req[i].init_time,
                        req_bat->req[i].addr, cur_posx, req_bat->req[i].posy, tag->conn, tag->posx, tag->posy, p_str);
                        in_range_req_num++;
                        in_range_req = &req_bat->req[i];
                    }else{
                        LOG(INFO, "req_start_time:%d, [in-veiw but intersect]init_time:%d, reader[%d](%f, %f)-->tag[%d](%f, %f), receive data:%s",req_bat->req[i].start_time, req_bat->req[i].init_time,
                        req_bat->req[i].addr, cur_posx, req_bat->req[i].posy, tag->conn, tag->posx, tag->posy, p_str);
                    }
                }
                lights.pop_back();
            }
            if(in_range_req_num > 1){
                LOG(INFO, "DOWNLINK COLLISION");
                send_nop_to_proxy(tag);
                //downlink collision
            }else if(in_range_req_num == 1){
                parse_downlink(tag, in_range_req);      //parse downlink and send response 
            }else{
                send_nop_to_proxy(tag);
                //out of range
            }
            pthread_mutex_unlock(&tag_mutex);
        }
        free(req_bat);
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




void *tag_proxy(void *arg){
    double tag_pos[TAG_MAX_NUM];
    sign_info_t sign_info[TAG_MAX_NUM];

    memset(tag_pos, 0, sizeof(tag_pos));
    memset(sign_info, 0, sizeof(sign_info_t));

    int tag_conn[TAG_MAX_NUM];

    int local_serverfd = unix_domain_server_init(tag_proxy_path);

    pthread_t *thread_tab = (pthread_t *)malloc(sizeof(pthread_t)*tag_num);

    int tag_index = 0;
    for(int i = 0; i < TAG_AXIS_NUM; i++){
//        tag_pos[tag_index++] = TAG_SPACING_OFFSET + i * g_spacing;        
        sign_info[tag_index].posx = TAG_SPACING_OFFSET + i * g_spacing;
        sign_info[tag_index].type = LARGE_SIGN;
        tag_index++;

        int internal_tag_num = random()%EACH_SPACING_MAX_TAG_NUM;
        double insert_tag[10];
        int insert_count = 0;
        for(int j = 0; j < internal_tag_num; j++){
            insert_tag[insert_count++] = TAG_SPACING_OFFSET + i * g_spacing + random()%(int)g_spacing;  
        }
        sort(insert_tag, insert_tag+insert_count);
        for(int j = 0; j < internal_tag_num; j++){
            sign_info[tag_index].posx = insert_tag[j];
            sign_info[tag_index].type = random()%2 + SMALL_SIGN;
            tag_index++;
        }
//        memcpy(&tag_pos[tag_index], insert_tag, insert_count * sizeof(double));
//        tag_index += insert_count;
    }
    tag_num = tag_index;
    for(int i = 0; i < tag_num; i++){
        printf("tag[%d]:(%f,%f), type:%d\n", i, sign_info[i].posx, 0.0, sign_info[i].type) ;
//        tag_pos[i] = i * g_spacing + TAG_SPACING_OFFSET;
//        pthread_create(&thread_tab[i], NULL, tag_thread, (void *)&tag_pos[i]);
        pthread_create(&thread_tab[i], NULL, tag_thread, (void *)&sign_info[i]);
    }   


    int remote_serverfd = unix_domain_server_init(simulator_server_path);

    for(int i = 0; i < tag_num; i++){
        tag_conn[i] = accept(local_serverfd, NULL, NULL);
    }

    int remote_conn = accept(remote_serverfd, NULL, NULL);


//    char tag_num_str[32];
//    sprintf(tag_num_str, "%d", tag_num);
//    write(remote_serverfd, tag_num_str, )
    write(remote_conn, tag_pos, sizeof(tag_pos));


    printf("tag_proxy init ok\n");

    while(1){
        fd_set fds;
        FD_ZERO(&fds);
        int maxfd = -1;
        FD_SET(remote_conn, &fds);
        maxfd = remote_conn;
//        FD_SET(trace_fd, &fds);
//        maxfd = remote_conn > trace_fd ? remote_conn : trace_fd;
        int ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
        req_bat_t *req_bat = (req_bat_t *)malloc(sizeof(req_bat_t));
        memset(req_bat, 0, sizeof(req_bat_t));
        rsp_bat_t *rsp_bat = (rsp_bat_t *)malloc(sizeof(rsp_bat_t));
        memset(rsp_bat, 0, sizeof(rsp_bat_t));
        if(ret > 0){
            if(FD_ISSET(remote_conn, &fds)){        //1.recvive from reader proxy
                read(remote_conn, (char *)req_bat, sizeof(req_bat_t)); 
                
                for(int i = 0; i < tag_num; i++){       //2.send req to tag
                    write(tag_conn[i], (char *)req_bat, sizeof(req_bat_t));
                }
                
                int receive_remain = tag_num;
                while(receive_remain){
                    fd_set tag_fds;
                    int tag_maxfd = -1;
                    FD_ZERO(&tag_fds);
                    for(int i = 0; i < tag_num; i++){
                        FD_SET(tag_conn[i], &tag_fds);
                        tag_maxfd = tag_maxfd > tag_conn[i] ? tag_maxfd : tag_conn[i];
                    }
                    int tag_ret = select(tag_maxfd + 1, &tag_fds, NULL, NULL, NULL);     //3.wait for tag response, then send to reader proxy
                    if(tag_ret > 0){
                        for(int i = 0; i < tag_num; i++){
                            if(FD_ISSET(tag_conn[i], &tag_fds)){
                                receive_remain--;
                                tag_response_t tag_rsp;
                                read(tag_conn[i], (char *)&tag_rsp, sizeof(tag_response_t));    
                                if(tag_rsp.type == UPLINK_CANNOT_OUT){
                                    
                                }else{
                                    memcpy(&rsp_bat->rsp[rsp_bat->num++], (char *)&tag_rsp, sizeof(tag_response_t));
                                }
                            }
                        }
                //        LOG(DEBUG, "receive_remain:%d, rsp_bat->num:%d", receive_remain, rsp_bat->num);
                    }else if(tag_ret == 0){
                        LOG(DEBUG, "rsp_bat->num:%d", rsp_bat->num);
                        //write(remote_conn, (char *)rsp_bat, sizeof(rsp_bat_t));
                        break;                        
                    }else{
                        //something error!
                    }
                }
                LOG(DEBUG, "rsp_bat->num:%d", rsp_bat->num);
                write(remote_conn, (char *)rsp_bat, sizeof(rsp_bat_t));
            }

#if 0
            if(FD_ISSET(trace_fd, &fds)){   //receive from trace server
                FD_SET(trace_fd, &fds);
                char recvbuf[64];
                int buflen = 0;
                buflen = read(trace_fd, recvbuf, 64);
                if(buflen >= 5 && (memcmp(recvbuf, "RESET", 5) == 0)){
                    printf("[%s]--RESET\n", __func__);
                    for(int j = 0; j < tag_num; j++){
                        write(tag_conn[j], "RESET", 5);
                    }
                }else if(buflen >= 4 && (memcmp(recvbuf, "KILL", 4) == 0)){
                    for(int j = 0; j < tag_num; j++){
                        write(tag_conn[j], "KILL", 4);
                    }
                    break;
                }
            }
#endif            
        }
        free(req_bat);
        free(rsp_bat);
    
    }
    for(int i = 0; i < tag_num; i++){
        pthread_join(thread_tab[i], NULL);
    }   
    free(thread_tab);

}




void usage(char *prog){
    printf("Usage:%s <tag_num> <spacing> \n", prog);
}


//reader_num<int>
//tag_num<int>
//g_spaceing<double>

int main(int argc, char *argv[]){
    if(argc == 2){
//        reader_num = atoi(argv[1]);
//        tag_num = atoi(argv[1]);
        g_spacing = atof(argv[1]);
//        tag_num = TAG_ROAD_LENGTH/g_spacing;
//        g_velocity = atof(argv[4])/3600;
        g_com_dist_down = DOWNLINK_DISTANCE;
        g_sys_fov = 20.0/2*PI/180;
    }else{
        usage(argv[0]);
        return 0;
    }
    init_random();

    pthread_mutex_init(&tag_mutex, 0);

    trace_fd = unix_domain_client_init(trace_sock_path);

    pthread_t thread_proxy;
    pthread_create(&thread_proxy, NULL, tag_proxy, NULL);
    printf("init ok\n");

    pthread_join(thread_proxy, NULL);
    destroy_random();
}

