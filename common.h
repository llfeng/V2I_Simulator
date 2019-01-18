/*
 * =====================================================================================
 *
 *       Filename:  common.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2019年01月08日 15时58分13秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  lilei.feng (), lilei.feng@pku.edu.cn
 *        Company:  Peking University
 *
 * =====================================================================================
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#define REQ_MAX_NUM 4
#define RSP_MAX_NUM 6


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



//response type
#define DISC_ACK    1
#define DATA        2

typedef struct{
    int type;
    int plen;
    int start_time;
    char payload[32];
}tag_response_t;

typedef struct{
    int conn;
    int addr;
    int posx;
    int posy;
    int plen;
    int start_time;
    int backoff_time;
    int elapsed_time;
    char payload[32];
}reader_request_t; 


//tag response batch type
#define INIT_TRIGGER    1
#define TAG_RSP         2

typedef struct{
    int num;
    reader_request_t req[REQ_MAX_NUM];
}req_bat_t;

typedef struct{
    int type;
    int num;
    int sent;
    tag_response_t rsp[RSP_MAX_NUM];
}rsp_bat_t;




#define RECORD_TRACE(sockfd, s, format, ...)\
do{ \
    sprintf(s, format, ##__VA_ARGS__);\
    write(sockfd, s, strlen(s)); \
}while(0)


extern void init_random();

extern uint64_t get_random();

extern void destroy_random();

extern int unix_domain_server_init(const char *path);

extern int unix_domain_client_init(const char *path);

#endif
