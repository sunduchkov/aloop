/*
 *
 */

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "sendstates.h"

int sendstates_start(sendstates_t* p, char* network_interface)
{
	if(!p) {
		printf("sendstates_start: null pointer\n");
		return 0;
	}

	if(-1 == (p->sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))) {
		printf("sendstates_start:socket (%s)\n", strerror(errno));
		return 0;
	}

#if 1 // enable broadcast
	int broadcastEnable=1;
	if(-1 == setsockopt(p->sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable))) {
		printf("sendstates_start:setsockopt (%s)\n", strerror(errno));
		return 0;
	}
#endif

	if(network_interface) {
		if(-1 == (setsockopt(p->sockfd, SOL_SOCKET,  SO_BINDTODEVICE, network_interface, strlen(network_interface)))) {
			printf("setsockopt:SO_BINDTODEVICE (%s)\n", strerror(errno));
			if (errno == EPERM)
				printf("You must obtain superuser privileges to bind a socket to device\n");
	        else
	        	printf("Cannot bind socket to device\n");
			return 0;
		}
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SENDSTATES_PORT);

	if(-1 == bind(p->sockfd, (struct sockaddr*)&addr, sizeof(addr))) {
		printf("sendstates_start:bind (%s)\n", strerror(errno));
		return 0;
	}

	memset((char*)&p->servaddr, 0, sizeof(p->servaddr));
	p->servaddr.sin_family = AF_INET;
	p->servaddr.sin_port = htons(SENDSTATES_PORT);
	p->servaddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
//	p->servaddr.sin_addr.s_addr = inet_addr(SENDSTATES_GROUP); // multi-cast

	return 1;
}

int sendstates_stop(sendstates_t* p)
{
	if(!p) {
		printf("sendstates_stop: null pointer\n");
		return 0;
	}

	if(p->sockfd) {
		if(-1 == (close(p->sockfd))) {
			printf("sendstates_stop:socket (%s)\n", strerror(errno));
			return 0;
		}
		p->sockfd = 0;
	}

	return 1;
}

int sendstates_send(sendstates_t* p, void* pBuf, int nLength)
{
	if(!p) {
		printf("sendstates_send: null pointer\n");
		return 0;
	}

	if (sendto(p->sockfd, pBuf, nLength, 0 , (struct sockaddr *)&p->servaddr, sizeof(p->servaddr))== -1) {
		printf("sendstates_send:sendto (%s)\n", strerror(errno));
		return 0;
	}

	return 1;
}

