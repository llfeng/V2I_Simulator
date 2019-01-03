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
#include <sys/un.h>  
#include <errno.h>  
#include <string.h>  
#include <unistd.h>  
#include <fcntl.h>
#include <errno.h>
#include <time.h>

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
}tag_alias_t;

typedef struct{
    char addr;
    uplink_frame_t uplink_frame;
    reader_t reader[READER_MAX_NUM];
    char silent[READER_MAX_NUM];
    char silent_num;
    char txbuflen;
    char txbuf[FRAME_MAX_LEN];
    char alias_num;
    tag_alias_t alias[READER_MAX_NUM];
    char tx_ready;
}tag_t;

tag_t tag_item_table[TAG_MAX_NUM];

char *server_path = "server.socket";  


int reader_num = 2;
int tag_num = 4;


int server_listen() {  
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0){
        perror("create sock fail:");
    }
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, server_path, sizeof(addr.sun_path)-1);
    unlink(server_path);
	if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("bind fail:");
    }
    if(listen(fd, reader_num) < 0){
        perror("listen fail:");
    }
	return fd;
}

int simulator_server(){         //tag is server, reader is client.
    int count = 0;
    int fd = server_listen();
    for(int i = 0; i < tag_num; i++){
        for(int j = 0; j < reader_num; j++){
            
            tag_item_table[i].reader[j].conn = accept(fd, NULL, NULL);            
            if(tag_item_table[i].reader[j].conn > 0){
                count++;
                printf("accept:%d\n", count);
            }else{
                perror("accept fail:");
            }
        }
    }
}

int recv_from_reader(){
    fd_set fds;
    FD_ZERO(&fds);
    int max_fd = -1;
    for(int i = 0; i < tag_num; i++){
        for(int j = 0; j < reader_num; j++){
            max_fd = max_fd > tag_item_table[i].reader[j].conn ? max_fd : tag_item_table[i].reader[j].conn;
            FD_SET(tag_item_table[i].reader[j].conn, &fds);
        }
    }
    int recv_flag = 0;
    while(1){    
        struct timeval tv; 
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int ret = select(max_fd, &fds, NULL, NULL, &tv);
        printf("[%s] ret:%d\n", __func__, ret);
        if(ret > 0){ 
            for(int i = 0; i < tag_num; i++){
                for(int j = 0; j < reader_num; j++){
                    if(FD_ISSET(tag_item_table[i].reader[j].conn, &fds)){
                        tag_item_table[i].reader[j].rxbuflen = read(tag_item_table[i].reader[j].conn, tag_item_table[i].reader[j].rxbuf, FRAME_MAX_LEN);
                    }
                }
            }
            recv_flag = 1;
        }else{
            if(recv_flag){
                return 1;
            }
        }
    }
    return 0;
}



//the identification is UUID, not addr.
void keep_silent(int tag_addr, int reader_addr){
    for(int i = 0; i < tag_num; i++){
        if(tag_item_table[i].addr == tag_addr){
            tag_item_table[i].silent[tag_item_table[i].silent_num++] = reader_addr;
            break;
        }
    }
}

int is_silent(int tag_addr, int reader_addr){
    for(int i = 0; i < tag_num; i++){
        if(tag_item_table[i].addr == tag_addr){
            for(int j = 0; j < tag_item_table[i].silent_num; j++){
                if(tag_item_table[i].silent[j] == reader_addr){
                    return 1;
                }
            }
        }
    }
    return 0;
}

void deframe(){
    for(int i = 0; i < tag_num; i++){
        for(int j = 0; j < reader_num; j++){
            tag_item_table[i].reader[j].downlink_frame.src = tag_item_table[i].reader[j].rxbuf[0];
            tag_item_table[i].reader[j].downlink_frame.type = (tag_item_table[i].reader[j].rxbuf[1] >> 4);
            tag_item_table[i].reader[j].downlink_frame.dst = (tag_item_table[i].reader[j].rxbuf[1] & 0x0F);
        }
    }    
}

void parse_downlink(){
    deframe();
    for(int i = 0; i < tag_num; i++){
        for(int j = 0; j < reader_num; j++){
            if(tag_item_table[i].reader[j].downlink_frame.type == DISCOVERY_REQUEST){       //ACK last tag, block ack for other readers' tags. Assign address for tags haven't been discovered yet.
                printf("[%s]---recv DISCOVERY REQUEST\n", __func__);

                for(int k = 0; k < tag_item_table[i].alias_num; k++){
                    if(tag_item_table[i].alias[k].reader == tag_item_table[i].reader[j].downlink_frame.src &&
                    tag_item_table[i].alias[k].tag == tag_item_table[i].reader[j].downlink_frame.dst){     //ACK last tag
                        keep_silent(tag_item_table[i].addr, tag_item_table[i].reader[j].downlink_frame.src);
                    }
                    for(int m = 0; m < tag_item_table[i].reader[j].downlink_frame.addr_pair_num; m++){      //block ack
                        if(tag_item_table[i].alias[k].reader == tag_item_table[i].reader[j].downlink_frame.addr_pair[m].reader &&
                        tag_item_table[i].alias[k].tag == tag_item_table[i].reader[j].downlink_frame.addr_pair[m].tag){
                            keep_silent(tag_item_table[i].addr, tag_item_table[i].reader[j].downlink_frame.src);
                        }
                    }
                }


                if(!is_silent(tag_item_table[i].addr, tag_item_table[i].reader[j].downlink_frame.src)){ //has been acked.
                    //do nothing
                }else{
                    tag_item_table[i].reader[j].downlink_frame.collision_num = (tag_item_table[i].reader[j].rxbuf[2] >> 4);
                    tag_item_table[i].reader[j].downlink_frame.plen = (tag_item_table[i].reader[j].rxbuf[2] & 0x0F);                
                    if(tag_item_table[i].reader[j].downlink_frame.collision_num == 0){
                        tag_item_table[i].addr = 0;
                    }else{
                        tag_item_table[i].addr = random()%(2*tag_item_table[i].reader[j].downlink_frame.collision_num);
                    }
                    if(tag_item_table[i].addr == 0){    //prepare for uplink
                        tag_item_table[i].tx_ready = 1;
                        tag_item_table[i].uplink_frame.dst = tag_item_table[i].reader[j].downlink_frame.src;
                        tag_item_table[i].uplink_frame.src = tag_item_table[i].addr;
                        tag_item_table[i].uplink_frame.plen = 0;
                    }
                }

            }else{  //QUERY_REQUEST (ACK, NACK, CACK)
                //keep silent for last tag
                if(tag_item_table[i].reader[j].downlink_frame.type == QUERY_REQUEST_ACK){
                    keep_silent(tag_item_table[i].reader[j].downlink_frame.dst - 1, tag_item_table[i].reader[j].downlink_frame.src);
                }
                if(tag_item_table[i].reader[j].downlink_frame.dst == tag_item_table[i].addr){      //for me.
                    tag_item_table[i].uplink_frame.dst = tag_item_table[i].reader[j].downlink_frame.src;
                    tag_item_table[i].uplink_frame.src = tag_item_table[i].addr;
                    tag_item_table[i].uplink_frame.plen = 0;
                }else{  //not for me. needn't response

                }
            }
        }
    }
}


void enframe(){
    for(int i = 0; i < tag_num; i++){
        if(tag_item_table[i].uplink_frame.dst > 0){
            for(int j = 0; j < reader_num; j++){
                tag_item_table[i].txbuf[0] = tag_item_table[i].uplink_frame.dst;
                tag_item_table[i].txbuf[1] = (tag_item_table[i].uplink_frame.src << 4);
                tag_item_table[i].txbuf[1] += tag_item_table[i].uplink_frame.plen;
                tag_item_table[i].txbuflen = 2;
            }
        }
    }
}

void send_to_reader(){
    enframe();
    for(int i = 0; i < tag_num; i++){
        if(tag_item_table[i].uplink_frame.dst > 0){
            for(int j = 0; j < reader_num; j++){
                 int sent_bytes = write(tag_item_table[i].reader[j].conn, tag_item_table[i].txbuf, FRAME_MAX_LEN);
                 if(sent_bytes == tag_item_table[i].txbuflen){ 
                 //send success.
                 }
            }
        }
    }
}

void main(){
    srand(time(NULL));
    simulator_server();
    while(1){
        if(recv_from_reader()){
            parse_downlink();
            send_to_reader();        
        }
    }    
}

