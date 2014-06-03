/*
 *
 */

#ifndef __SENDSTATED_H__
#define __SENDSTATED_H__

#define SENDSTATES_PORT 6666

#include <netinet/in.h>

typedef struct
{
	int 	sockfd;
	struct sockaddr_in servaddr;

}	sendstates_t;

int sendstates_start(sendstates_t* p);
int sendstates_stop(sendstates_t* p);
int sendstates_send(sendstates_t* p, void* pBuf, int nLength);

#endif // __SENDSTATED_H__

