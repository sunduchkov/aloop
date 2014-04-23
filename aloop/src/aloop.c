/*
 *	Audio path-through
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <pthread.h>

#include "getparams.h"
#include "alsa_driver.h"

#define REALTIMEAUDIO_ENABLED	1

typedef struct
{
	alsa_driver_t 	driver;
	getparams_t		params;
	int				loop;

}	aadsp_t;

void setscheduler(void)
{
	struct sched_param sched_param;

	if (sched_getparam(0, &sched_param) < 0) {
		printf("Scheduler getparam failed...\n");
		return;
	}
	sched_param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if (!sched_setscheduler(0, SCHED_FIFO, &sched_param)) {
		printf("Scheduler set with priority %i...\n", sched_param.sched_priority);
		fflush(stdout);
		return;
	}
	printf("!!!Scheduler set with priority %i FAILED!!!\n", sched_param.sched_priority);
}

#if MMAP_ACCESS_ENABLED
static void ProcessStereo(int* samplesIn[2], int* samplesOut[2], int N)
{
	int i;
	int L, R;
	//static int k = 0;

	for(i = 0; i < N; ++i)
	{
#if CAPTURE_ENABLED
		L = samplesIn[0][i];
		R = samplesIn[1][i];
#else
		// Square signal to Left channel
		if(i & 0x40) L = (1<<30); else L = -(1<<30);
		R = L;
#endif

		// Spectrum inversion for Right channel
		//if(i & 1) R = -R;

		samplesOut[0][i] = L;
		samplesOut[1][i] = R;
	}
}
#else
static void ProcessStereo(int* samplesIn, int* samplesOut, int N)
{
	int i;
	int L, R;
	//static int k = 0;
	for(i = 0; i < N; ++i)
	{
#if CAPTURE_ENABLED
		L = samplesIn[i*2];
		R = samplesIn[i*2+1];
#else
		// Square signal to Left channel
		if(i & 0x40) L = (1<<30); else L = -(1<<30);
		R = L;
#endif

		// Spectrum inversion for Right channel
		//if(i & 1) R = -R;

		samplesOut[i*2] = L;
		samplesOut[i*2+1] = R;
	}
}
#endif

static void* realtime_audio(void* p)
{
	aadsp_t* aadsp = (aadsp_t*)p;
	alsa_driver_t* driver = &aadsp->driver;
	int r, w;
	int avail;

	if(0 == alsa_driver_new(driver))
	{
		printf("alsa_driver_new not succeeded\n");
		return 0;
	}

	printf("Audio Interface \"%s\" initialized with %d [ms] latency\n", driver->alsa_driver_name, driver->latency);

	if(!alsa_driver_prepare(driver))
	{
		printf("alsa_driver_start not succeeded\n");
		return 0;
	}

	printf("Audio Interface prepared for start\n");

	alsa_driver_start(driver);

	r = 0;
	w = 0;

	aadsp->loop = 1;

	while (aadsp->loop)
	{
		alsa_driver_wait(driver);

		avail = alsa_driver_read(driver);
		r += avail;
		if(r >= driver->sample_rate)
		{
			printf(".");
			fflush(stdout);
			r = 0;
		}

		avail = alsa_driver_write(driver, ProcessStereo);

		w += avail;
		if(w >= driver->sample_rate)
		{
			printf("+");
			fflush(stdout);
			w = 0;
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	aadsp_t aadsp;

	int err;

	printf("ALSA Path-through starting\n");

	if(0 == (err = alsa_driver_get_options(&aadsp.driver, argc, argv)))
	{
		exit(1);
	}

#if REALTIMEAUDIO_ENABLED
	pthread_t thread;
	pthread_attr_t attr;

	if(0 != pthread_attr_init(&attr)) {
		printf("pthread_attr_init: error");
		exit(1);
	}

	if(0 != pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
		printf("pthread_attr_setdetachstate: error");
		exit(1);
	}

	if(0 != pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
		printf("pthread_attr_setinheritsched: error");
		exit(1);
	}

	if(0 != (err = pthread_create(&thread, &attr, realtime_audio, &aadsp)))
	{
		if(EAGAIN == err) {
			printf("pthread_create: \n");
		}
		if(EINVAL == err) {
			printf("pthread_create: Invalid settings in attr\n");
		}
		if(EPERM == err) {
			printf("pthread_create:No permission to set the scheduling policy and parameters specified in attr\n");
		}
		exit(1);
	}

	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if(0 != pthread_setschedparam(thread, SCHED_FIFO, &param)) {
		printf("pthread_setschedparam: error\n");
		exit(1);
	}
#endif

	if(!getparams_start(&aadsp.params)) {
		exit(1);
	}

	printf("Waiting for connection...\n");

	while(1) {
		if(getparams_connect(&aadsp.params)) {
			printf("Incoming connection accepted\n");
		}

		if(getparams_get(&aadsp.params)) {
			printf("%d %f\n", aadsp.params.nNumber, aadsp.params.fValue);
		}
	}

	getparams_stop(&aadsp.params);

#if REALTIMEAUDIO_ENABLED
	if(0 != pthread_join(thread, NULL)) {
		printf("pthread_join: error");
		exit(1);
	}
#endif

	return 0;
}
