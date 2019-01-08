/*
 * =====================================================================================
 *
 *       Filename:  trace.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2019年01月08日 15时38分17秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  lilei.feng (), lilei.feng@pku.edu.cn
 *        Company:  Peking University
 *
 * =====================================================================================
 */

#ifndef __TRACE_H__
#define __TRACE_H__

int trace_sys_init();

void trace_sys_destroy(int fd);


#define RECORD_TRACE(format, ...)\
do{         \
    fprintf(trace_fd, format, ##__VA__ARGS__);\
}while(0)

#endif
