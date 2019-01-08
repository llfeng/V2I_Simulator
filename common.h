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


#define RECORD_TRACE(sockfd, s, format, ...)\
do{ \
    sprintf(s, format, ##__VA_ARGS__);\
    write(sockfd, s, strlen(s)); \
}while(0)



extern int unix_domain_server_init(char *path);

extern int unix_domain_client_init(char *path);

#endif
