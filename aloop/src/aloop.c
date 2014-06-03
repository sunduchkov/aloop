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
#include "sendstates.h"
#include "alsa_driver.h"

#define REALTIMEAUDIO_ENABLED	0
#define NETWORKING_ENABLED		1

typedef struct
{
	alsa_driver_t 	driver;
	getparams_t		getparams;
	sendstates_t	sendstates;
	int				loop;

	int 			gain;

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

#if REALTIMEAUDIO_ENABLED
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
static void ProcessStereo(aadsp_t* p, int* samplesIn, int* samplesOut, int N)
{
	int i;
	int L, R;
	int64_t acc;

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

		acc = (int64_t)L * p->gain;
		L = (int)(acc >> 31);
		acc = (int64_t)R * p->gain;
		R = (int)(acc >> 31);

		samplesOut[i*2] = L;
		samplesOut[i*2+1] = R;
	}
}
#endif

static void* realtime_audio(void* p)
{
	aadsp_t* aadsp = (aadsp_t*)p;
	alsa_driver_t* driver = &aadsp->driver;
	int* addr;
	int size;

	while(1)
	{
		if(ALSA_STATUS_OK != alsa_driver_prepare(driver)) {
			printf("alsa_driver_prepare not succeeded\n");
			continue;
		}

		printf("Audio Interface prepared for start\n");

		if(ALSA_STATUS_OK != alsa_driver_start(driver)) {
			printf("alsa_driver_start not succeeded\n");
			continue;
		}

		aadsp->loop = 1;

		aadsp->gain = 0x7fffffff;

		while (aadsp->loop)
		{
			if(ALSA_STATUS_OK != alsa_driver_wait(driver, NULL)) {
				break;
			}

			if(ALSA_STATUS_OK != alsa_driver_read(driver)) {
				break;
			}

			if(ALSA_STATUS_OK != alsa_driver_write_prepare(driver, &addr, &size)) {
				break;
			}

			ProcessStereo(aadsp, addr, addr, size);

			if(ALSA_STATUS_OK != alsa_driver_write(driver)) {
				break;
			}
		}

		printf("ALSA restart\n");
	}

	return NULL;
}
#endif

const char* pthread_err(int err)
{
	static const char sEAGAIN[] = "Insufficient  resources  to  create another thread";
	static const char sEINVAL[] = "Invalid settings";
	static const char sEPERM[] = "No permission to set the scheduling policy and parameters";
	static const char sESRCH[] = "No thread with the ID thread could be found";
	static const char sENOTSUP[] = "Unsupported value";
	static const char sUNKNOWN[] = "Unrecognized error";
	const char* str = NULL;

	if(EAGAIN == err) {
		str = sEAGAIN;
	}
	else if(EINVAL == err) {
		str = sEINVAL;
	}
	else if(EPERM == err) {
		str = sEPERM;
	}
	else if(ESRCH == err) {
		str = sESRCH;
	}
	else if(ENOTSUP == err) {
		str = sENOTSUP;
	}
	else {
		str = sUNKNOWN;
	}

	return str;
}

int main(int argc, char *argv[])
{
	aadsp_t aadsp;
	alsa_driver_t* driver = &aadsp.driver;

	printf("ALSA Path-through starting...\n");

#if REALTIMEAUDIO_ENABLED
	if(ALSA_STATUS_OK != alsa_driver_get_options(&aadsp.driver, argc, argv)) {
		exit(1);
	}

	if(ALSA_STATUS_OK != alsa_driver_open(driver)) {
		printf("alsa_driver_new not succeeded\n");
		exit(1);
	}

	printf("Audio Interface \"%s\" initialized with %d [ms] latency\n", driver->alsa_driver_name, driver->latency);

	pthread_t thread;
	pthread_attr_t attr;
	int err;

	if(0 != (err = pthread_attr_init(&attr))) {
		printf("pthread_attr_init: error (%s)\n", pthread_err(err));
		exit(1);
	}

	if(0 != (err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
		printf("pthread_attr_setdetachstate: error (%s)\n", pthread_err(err));
		exit(1);
	}

	if(0 != (err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))) {
		printf("pthread_attr_setinheritsched: error (%s)\n", pthread_err(err));
		exit(1);
	}

	if(0 != (err = pthread_create(&thread, &attr, realtime_audio, &aadsp)))	{
		printf("pthread_create: error (%s)\n", pthread_err(err));
		exit(1);
	}

	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if(0 != (err = pthread_setschedparam(thread, SCHED_FIFO, &param))) {
		printf("pthread_setschedparam: error (%s)\n", pthread_err(err));
		exit(1);
	}
#endif

#if NETWORKING_ENABLED
	if(!getparams_start(&aadsp.getparams)) {
		exit(1);
	}

	if(!sendstates_start(&aadsp.sendstates)) {
		exit(1);
	}

	printf("Networking started\n");
#endif

	while(1) {
#if NETWORKING_ENABLED
		sendstates_send(&aadsp.sendstates, &aadsp.gain, sizeof(aadsp.gain));

		if(getparams_connect(&aadsp.getparams)) {
			printf("Incoming connection accepted\n");
		}

		if(getparams_get(&aadsp.getparams)) {
			printf("%d %f\n", aadsp.getparams.nNumber, aadsp.getparams.fValue);

			if(7 == aadsp.getparams.nNumber) {
				aadsp.gain = (int)((int64_t)0x7fffffff * aadsp.getparams.fValue / 100);
				printf("%x\n", aadsp.gain);
			}
		}

		sleep(0);
#endif
	}

	getparams_stop(&aadsp.getparams);
	sendstates_stop(&aadsp.sendstates);

#if REALTIMEAUDIO_ENABLED
	if(0 != pthread_join(thread, NULL)) {
		printf("pthread_join: error");
		exit(1);
	}
#endif

	alsa_driver_close(driver);

	return 0;
}
