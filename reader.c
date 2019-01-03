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
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>  
#include <fcntl.h>
#include <time.h>


#define DOWN_SLOT_TIME   60
#define DOWNLINK_WINDOW 20
#define DOWNLINK_BITRATE 1000

#define FRAME_MAX_LEN   32
#define PAYLOAD_MAX_LEN 24

#define READER_MAX_NUM  10
#define TAG_MAX_NUM  10

#define DISCOVERY_REQUEST   1
#define QUERY_REQUEST   2
#define QUERY_REQUEST_ACK   3
#define QUERY_REQUEST_NACK  4
#define QUERY_REQUEST_CACK  5

typedef struct{
    char reader;
    char tag;
}addr_pair_t;

typedef struct{
    char src;
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
    int conn;
    int rxbuflen;
    char rxbuf[FRAME_MAX_LEN];
    uplink_frame_t uplink_frame;
}tag_t;

typedef struct{
    char addr;
    downlink_frame_t downlink_frame;
    tag_t tag[TAG_MAX_NUM];
    char txbuflen;
    char txbuf[FRAME_MAX_LEN];
    int start_time;
    int end_time;
    char last_query_addr;
}reader_t;


int reader_num = 2;
int tag_num = 4;

reader_t reader_item_table[READER_MAX_NUM];

char *server_path = "server.socket";

int reader_fd_init(){
    int fd = socket(AF_UNIX, SOCK_STREAM, 0); 
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server_path, sizeof(addr.sun_path)-1);
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    return fd; 
}




void readers_connect(){
    for(int i = 0; i < tag_num; i++){
        for(int j = 0; j < reader_num; j++){
            reader_item_table[j].tag[i].conn = reader_fd_init();             //[reader_seq][tag_seq]
        }
    }
}

int gen_start_time(){
    int slot_time = DOWN_SLOT_TIME;
    int offset = random()%DOWN_SLOT_TIME;
    return offset + (random() % DOWNLINK_WINDOW) * DOWN_SLOT_TIME;
}

void send_discovery_request(reader_t *reader){
    for(int i = 0; i < tag_num; i++){
        int sent_bytes = write(reader->tag[i].conn, reader->txbuf, reader->txbuflen);
        if(sent_bytes == reader->txbuflen){
            printf("send discovery request success\n");
        }else{
            printf("sent bytes:%d\n", sent_bytes);
        }
    }
}

/* 
void recv_discovery_ack(reader_t *reader){
    for(int i = 0; i < tag_num; i++){
        reader->tag[i].rxbuflen = read(reader->conn[i], reader->tag[i].rxbuf, FRAME_MAX_LEN);
    }
}
*/

void enframe_query(reader_t *reader){
    reader->txbuf[0] = reader->downlink_frame.src;
    reader->txbuf[1] = (reader->downlink_frame.type << 4);
    reader->txbuf[1] += reader->downlink_frame.dst;
}

void send_query_request(reader_t *reader, int tag_addr){
    reader->downlink_frame.dst = tag_addr;
    enframe_query(reader);
    for(int i = 0; i < tag_num; i++){
        int sent_bytes = write(reader->tag[i].conn, reader->txbuf, reader->txbuflen);
        if(sent_bytes == reader->txbuflen){
            //send success.
        }
    }
}

void recv_from_tag(){
    fd_set fds;
    FD_ZERO(&fds);
    int max_fd = -1;
    for(int i = 0; i < reader_num; i++){
        for(int j = 0; j < tag_num; j++){
            max_fd = max_fd > reader_item_table[i].tag[j].conn ? max_fd : reader_item_table[i].tag[j].conn;
            FD_SET(reader_item_table[i].tag[j].conn, &fds);
        }
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    while(1){
        int ret = select(max_fd, &fds, NULL, NULL, &tv);
        if(ret > 0){
            for(int i = 0; i < reader_num; i++){
                for(int j = 0; j < tag_num; j++){
                    if(FD_ISSET(reader_item_table[i].tag[j].conn, &fds)){
                        reader_item_table[i].tag[j].rxbuflen = read(reader_item_table[i].tag[j].conn, reader_item_table[i].tag[j].rxbuf, FRAME_MAX_LEN);
                    }
                }
            }
            break;
        }
    }
}

void deframe(){
    for(int i = 0; i < reader_num; i++){
        for(int j = 0; j < tag_num; j++){
            reader_item_table[i].tag[j].uplink_frame.dst = reader_item_table[i].tag[j].rxbuf[0];
            reader_item_table[i].tag[j].uplink_frame.src = (reader_item_table[i].tag[j].rxbuf[1] >> 4);
            reader_item_table[i].tag[j].uplink_frame.plen = (reader_item_table[i].tag[j].rxbuf[1] & 0x0F);
        }
    }
}

void parse_uplink(reader_t *reader){
    //uplink collision.
    deframe();

    int collision_flag = 0;
    int uplink_slot[TAG_MAX_NUM];
    memset(uplink_slot, 0, sizeof(uplink_slot));
    for(int i = 0; i < tag_num; i++){
        uplink_slot[reader_item_table[0].tag[i].uplink_frame.src]++;     
    }
    for(int i = 0; i < tag_num; i++){
        if(uplink_slot[i] > 1){
            collision_flag = 1;
            break;
        }
    }
    
    int recv_flag = 0;
    //uplink work well.
    if(!collision_flag){
        for(int i = 0; i < reader_num; i++){
            int addr_pair_index = reader_item_table[i].downlink_frame.addr_pair_num;
            for(int j = 0; j < tag_num; j++){
                if(reader_item_table[i].tag[j].uplink_frame.dst == reader_item_table[i].addr){    //for me
                    reader_item_table[i].downlink_frame.src = reader_item_table[i].addr;
                    reader_item_table[i].downlink_frame.type = QUERY_REQUEST_ACK;
                    reader_item_table[i].last_query_addr++;
                    recv_flag = 1;
                }else{      //not for me, save address pair.
                    reader_item_table[i].downlink_frame.addr_pair[addr_pair_index].reader = reader_item_table[i].tag[j].uplink_frame.dst;
                    reader_item_table[i].downlink_frame.addr_pair[addr_pair_index].tag = reader_item_table[i].tag[j].uplink_frame.src;
                    addr_pair_index++;
                }
            }
            reader_item_table[i].downlink_frame.addr_pair_num = addr_pair_index;
        }
        if(recv_flag == 0){
            reader->downlink_frame.src = reader->addr;
            reader->downlink_frame.type = QUERY_REQUEST_NACK;
            reader->last_query_addr++;
        }
    }else{
        reader->downlink_frame.src = reader->addr;
        reader->downlink_frame.type = QUERY_REQUEST_CACK;
        reader->last_query_addr++;
    }
}


reader_t *select_reader(){
    reader_t *reader_array[READER_MAX_NUM] = {NULL};

    for(int i = 0; i < reader_num; i++){
        reader_array[i] = &reader_item_table[i];
    }

    reader_t *min = NULL;
    for(int i = 0; i < reader_num; i++){
        min = reader_array[i];
        for(int j = i+1; j < reader_num; j++){            
            if(reader_array[j]->start_time < min->start_time){
                reader_array[i] = reader_array[j];        
                reader_array[j] = min;
                min = reader_array[i];
            }
        }
    }
    
    int side_count = 0;

    if(reader_num == 1){
        return reader_array[0];
    }else if(reader_num == 0){
        return NULL;
    }

    for(int i = 1; i < reader_num; i++){    
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

        if(i == reader_num){
            if(reader_array[i-1]->end_time < reader_array[i]->start_time){
                return reader_array[i];
            }
        }
    }
    return NULL;
}

void readers_init(){
    for(int i = 0; i < reader_num; i++){
        reader_item_table[i].txbuflen = 3;
        reader_item_table[i].txbuf[0] = 0xA0+i;
        reader_item_table[i].txbuf[1] = (DISCOVERY_REQUEST << 4);
        reader_item_table[i].txbuf[1] += 0xF;
        reader_item_table[i].txbuf[2] = 0;
    }
}

int main(){
    srand(time(NULL));
    readers_connect();    
    readers_init();
    while(1){
        for(int i = 0; i < reader_num; i++){
            reader_item_table[i].start_time = gen_start_time();
            reader_item_table[i].end_time = reader_item_table[i].start_time + (reader_item_table[i].txbuflen * 8)*1000/DOWNLINK_BITRATE;
        }
        reader_t *reader = select_reader();
        printf("p_reader:%p\n", reader);
        if(reader == NULL){         //downlink collision
            continue; 
        }else{
            printf("before send\n");
            send_discovery_request(reader);
            printf("after send\n");
//           recv_discovery_ack(reader);
            recv_from_tag(0);
            parse_uplink(reader);
            int tag_addr_range = 0;
            if(reader->downlink_frame.collision_num){
                tag_addr_range = reader->downlink_frame.collision_num*2;
            }else{
                tag_addr_range = 1;
            }
            for(int j = 1; j < tag_addr_range; j++){                
                send_query_request(reader, j);
                recv_from_tag();
                parse_uplink(reader);
            }
        }
    }
}
