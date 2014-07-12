/*
 *
 */

#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "getparams.h"

int getparams_connect(getparams_t* p)
{
	int connection;

	if(!p) {
		printf("getparams_connect: null pointer\n");
		return 0;
	}

	if(-1 == (connection = accept(p->sockfd, NULL, NULL))) {
		return 0;
	}

	if(p->connection) {
		printf("Close old connection\n");

		if(p->connection) {
			if(-1 == (close(p->connection))) {
				printf("getparams_stop:connection (%s)\n", strerror(errno));
				return 0;
			}
			p->connection = 0;
		}
	}

	p->connection = connection;

	return 1;
}


int getparams_get(getparams_t* p)
{
	char buf[1024];
	char* ptr;
	int size;

	if(!p) {
		printf("getparams_get: null pointer\n");
		return 0;
	}

	if(!p->connection) {
		return 0;
	}

	size = recv(p->connection, buf,sizeof(buf), 0);
	if(-1 == size) {
		printf("recv: (%s)\n", strerror(errno));
		return 0;
	}
	if(!size) {
		return 0;
	}

	ptr = strstr(buf, PARAM_PHRASE);
	if (ptr != 0) {
		char * data = &ptr[strlen(PARAM_PHRASE)+1];
		sscanf(data, "%u %lf", &p->nNumber, &p->fValue);
	}

	return 1;
}

int getparams_start(getparams_t* p, char* network_interface)
{
	if(!p) {
		printf("getparams_start: null pointer\n");
		return 0;
	}

	if(-1 == (p->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))) {
		printf("getparams_start: (%s)\n", strerror(errno));
		return 0;
	}

	int optval;

	if(-1 == (setsockopt(p->sockfd, SOL_SOCKET,  SO_REUSEADDR, (char *)&optval, sizeof(optval)))) {
		printf("setsockopt:SO_REUSEADDR (%s)\n", strerror(errno));
		return 0;
	}

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

#if 0
	if(-1 == ioctl(p->sockfd, FIONBIO, (char *)&optval)) {
		printf("ioctl: (%s)\n", strerror(errno));
		return 0;
	}
#else
	int flags;

	if(-1 == (flags = fcntl(p->sockfd, F_GETFL, 0))) {
		printf("fcntl: (%s)\n", strerror(errno));
		return 0;
	}
	if(-1 == fcntl(p->sockfd, F_SETFL, flags | O_NONBLOCK)) {
		printf("fcntl: (%s)\n", strerror(errno));
		return 0;
	}
#endif

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(GETPARAMS_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(-1 == bind(p->sockfd, (struct sockaddr*)&addr, sizeof(addr))) {
		printf("bind: (%s)\n", strerror(errno));
		return 0;
	}

	if(-1 == listen(p->sockfd, 10)) {
		printf("listen: (%s)\n", strerror(errno));
		return 0;
	}

	p->connection = 0;

	return 1;
}

int getparams_stop(getparams_t* p)
{
	if(!p) {
		printf("getparams_stop: null pointer\n");
		return 0;
	}

	if(p->connection) {
		if(-1 == (close(p->connection))) {
			printf("getparams_stop:connection (%s)\n", strerror(errno));
			return 0;
		}
		p->connection = 0;
	}

	if(p->sockfd) {
		if(-1 == (close(p->sockfd))) {
			printf("getparams_stop:socket (%s)\n", strerror(errno));
			return 0;
		}
		p->sockfd = 0;
	}

	return 1;
}
