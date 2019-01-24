/*
 * =====================================================================================
 *
 *       Filename:  proxy_server.cpp
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2019年01月16日 10时32分05秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  lilei.feng , lilei.feng@pku.edu.cn
 *        Company:  Peking University
 *
 * =====================================================================================
 */

#include <queue>
#include <algorithm>

using namespace std;

void update_tag_response(reader_t reader, tag_t *tag){
    tag->response
}

bool reader_cmp(reader_t *a, reader_t *b){
    return a->request.start_time > b->request.start_time;
}

typedef struct{
    char name[32];
    int time_ms;
    void (func)(int argc, void *argv);
}schedule_t;

schedule_t task[10];

void *timer_thread(){
    while(1){
        for(int i = 0; i < task_num; i++) {
            task[i]->time_ms--
            if(task[i]->time_ms == 0){
                task[i]->func(task[i]->argc, task[i]->argv);
                task[i] == NULL;
            }
        }
    }
}

void do_handler(reader_t *reader, tag_t *tag){
    //DOWNLINK COLLISION
    //DOWNLINK CARRIER BLOCK
    //DOWNLINK BLOCKAGE
    //UPLINK COLLISION
    //UPLINK IDLE
    //UPLINK DATA
    //UPLINK ACK
    reader_t *p_reader[READER_MAX_NUM];
    for(int i = 0; i < reader_num; i++){
        p_reader[i] = &reader[i];
    }
    sort(p_reader, p_reader + reader_num, reader_cmp);


    for(int i = 0; i < reader_num; i++){
        
    }
}

int main(){
    int proxy_serverfd = unix_domain_server_init(proxy_server_path);

    int reader_count = 0;
    reader_t reader_table[READER_MAX_NUM];

    int tag_count = 0;
    tag_t tag_table[TAG_MAX_NUM];

    for(int i = 0; i < reader_num + tag_num; i++){
        int conn = accept(proxy_serverfd, NULL, NULL);'
        char id_str[16];
        read(conn, id_str, sizeof(id_str));
        if(memcmp(id_str, "TAG", sizeof("TAG")) == 0){
            tag_table[tag_count++].conn = conn;
        }else if(memcmp(id_str, "READER", sizeof("READER")) == 0){
            reader_table[reader_count++].conn = conn;
        }
    }

    fd_set tag_fds;
    FD_ZERO(&tag_fds);

    fd_set reader_fds;
    FD_ZERO(&reader_fds);

    int tag_maxfd = -1;
    int reader_maxfd = -1;

    for(int i = 0; i < tag_num; i++){
        FD_SET(tag_table[i].conn, &tag_fds);
        tag_maxfd = tag_maxfd > tag_table[i].conn ? tag_maxfd : tag_table[i].conn;
    }

    for(int i = 0; i < reader_num; i++){
        FD_SET(reader_table[i].conn, &reader_fds);
        reader_maxfd = reader_maxfd > reader_table[i].conn ? reader_maxfd : reader_table[i].conn;
    }

    while(1){
        int recv_count = 0;
        while(1){
            int ret = select(reader_maxfd + 1, &reader_fds, NULL, NULL, NULL);
            if(ret > 0){
                for(int i = 0; i < reader_num; i++){
                    if(FD_ISSET(reader_table[i].conn), &fds){
                        read(reader_table[i].conn, (char *)&reader_table[i], sizeof(reader_t)); 
                        recv_count++;
                        if(recv_count == reader_num){
                            break;
                        }
                    }
                }
            }
        }

        while(1){
            int ret = select(tag_maxfd + 1, &tag_fds, NULL, NULL, NULL);
            if(ret > 0){
                for(int i = 0; i < tag_num; i++){
                    if(FD_ISSET(tag_table[i].conn), &fds){
                        read(tag_table[i].conn, (char *)&tag_table[i], sizeof(tag_t));
                        recv_count++;
                        if(recv_count == tag_num){
                            break;
                        }
                    }
                }
            }
        }
        do_handler(reader_table, tag_table);
    }
}
