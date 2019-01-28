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


#define RANDOM_INSERT_TAG 0

#define TAG_AXIS_NUM 20
#define EACH_SPACING_MAX_TAG_NUM 3

#define READER_ADDR_BITLEN  1
#define READER_ADDR_BITLEN_DEFAULT 1

#define USE_ROUND_ADDR  1

#define PREAMBLE_TIME   24

#define ENERGY_CHECK_TIME   24

#define DEFAULT_UPLINK_LEN  4
#define DEFAULT_DOWNLINK_LEN   (8 + READER_ADDR_BITLEN - READER_ADDR_BITLEN_DEFAULT)

#define UPLINK_ACK_TIME 24  //preamble


//#define DOWNLINK_WINDOW 8
#define DOWNLINK_WINDOW 20
#define DOWNLINK_BITRATE 10000

#define DOWN_SLOT_TIME   ((DEFAULT_DOWNLINK_LEN << 3)*1000/DOWNLINK_BITRATE)

#define UPLINK_BITRATE  128

#define FRAME_MAX_LEN   256
#define PAYLOAD_MAX_LEN 240

#define READER_MAX_NUM  100
//#define READER_MAX_NUM  200
#define TAG_MAX_NUM  (TAG_AXIS_NUM*EACH_SPACING_MAX_TAG_NUM)


#define REQ_MAX_NUM READER_MAX_NUM
#define RSP_MAX_NUM TAG_MAX_NUM


//response type
#define DISC_ACK    1
#define DATA        2
#define UPLINK_CANNOT_OUT   0xFF


//request type
#define REAL_SENT       1
#define NOT_REAL_SENT   2


#define SIGN_TYPE_MAX_NUM  LARGE_SIGN

#define SMALL_SIGN      1
#define LARGE_SIGN      2


typedef struct{
    double posx;
    int type;
} sign_info_t;



typedef struct{
    double posx;
    double posy;
    int type;
    int plen;
    int start_time;
    int sign_type;
    char payload[PAYLOAD_MAX_LEN];
}tag_response_t;

typedef struct{
    int conn;
    int addr;
    double posx;
    double posy;
    int type;
    int plen;
    int start_time;
    int init_time;
    int backoff_time;
    int elapsed_time;
    double velocity;
    char payload[PAYLOAD_MAX_LEN];
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

extern void get_send_pos(reader_request_t *reader_request, double *x, double *y);

extern void get_recv_pos(reader_request_t *reader_request, tag_response_t *tag_response, double *x, double *y);

extern void calibrate_reader_pos(reader_request_t *reader_request, int cur_time, double *x, double *y);

#endif
