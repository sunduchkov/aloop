/*
 *
 */

#ifndef __GETPARAMS_H__
#define __GETPARAMS_H__

#define GETPARAMS_PORT	9999
#define PARAM_PHRASE 	"param"

typedef struct
{
	int 	sockfd;
	int		connection;

	unsigned int	nNumber;
	double 			fValue;

}	getparams_t;

int getparams_start(getparams_t* p);
int getparams_connect(getparams_t* p);
int getparams_get(getparams_t* p);
int getparams_stop(getparams_t* p);

#endif // __GETPARAMS_H__

