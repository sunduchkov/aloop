/*
 *	Audio path-through (tested on Beagleboard_xM, Ubuntu 13.10)
 *
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.c
 */

#include <stdio.h>
#include "alsa_driver.h"

#define SAMPLERATE	48000
#define PERIOD_TIME	50                                  // PCM interrupt period [ms]
#define BUFFER_TIME	(2*PERIOD_TIME)                     // Buffer time [ms], should be at least 2xPERIOD_TIME
#define BUFFER_SIZE	(SAMPLERATE * BUFFER_TIME / 1000)   // sample rate [1/sec] x time [sec]
#define	LATENCY		(1000 * BUFFER_SIZE / SAMPLERATE)   // same as BUFFER_TIME [ms]

static void ProcessStereo(int* samplesIn, int* samplesOut, int N)
{
	int i;
	int L, R;
	//static int k = 0;
	for(i = 0; i < N; ++i)
	{
		L = samplesIn[i*2];
		R = samplesIn[i*2+1];

		// Spectrum inversion for Right channel
		//if(i & 1) R = -R;

		// Square signal to Left channel
		//if(i & 0x40) L = (1<<30); else L = -(1<<30);

		samplesOut[i*2] = L;
		samplesOut[i*2+1] = R;
	}
}

int main(int argc, char *argv[])
{
	int inbuf[BUFFER_SIZE*2]; // 2 - for stereo
	int outbuf[BUFFER_SIZE*2]; // 2 - for stereo

	alsa_driver_t* driver;

	printf("ALSA Path-through starting\n");

	memset(inbuf,0,sizeof(inbuf));
	memset(outbuf,0,sizeof(outbuf));

	if(NULL == (driver = alsa_driver_initialize()))
	{
		printf("driver_initialize not succeeded\n");
		return 0;
	}

	printf("ALSA driver initialized\n");

	alsa_driver_loop(driver, ProcessStereo);

	printf("ALSA Path-through can not run\n");

	return 0;
}
