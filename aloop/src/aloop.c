/*
 * Simple ALSA path-through program
 *
 * Based on article "Introduction to Sound Programming with ALSA" By Jeff Tranter
 *
 * http://www.linuxjournal.com/article/6735
 *
 * Combined capture/playback and added non-blocking mode
 *
 * Thanks to Paul Devis for introduction to ALSA
 *
 * author: Artem Sunduchkov
 *
 */

/* Use the newer ALSA API */
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <pthread.h>
#include <signal.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>

typedef struct
{
	int* buffer;
	int  frames;
	int  active;
	snd_pcm_t *handle_playback;
	snd_pcm_t *handle_capture;
	pthread_t realtime_audio_thread;

}	Audio_t;

static Audio_t Audio;

void process_audio(int* samples, int n)
{
	int i;
	int L, R;

	for(i = 0; i < n; ++i)
	{
		L = samples[i*2];
		R = samples[i*2+1];

		samples[i*2] = L;
		samples[i*2+1] = R;
	}
}

int open_stream(snd_pcm_t** handle, int dir, snd_pcm_uframes_t period)
{
	snd_pcm_hw_params_t *params;
	unsigned int val;
	int exactness;
	int rc;

	/* Open PCM device for capture/playback */
	if ((rc = snd_pcm_open(handle, "hw:0", dir, SND_PCM_NONBLOCK)) < 0) {
	    fprintf(stderr, "unable to open pcm device: %s\n", snd_strerror(rc));
	    return 0;
	}

	/* Allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(*handle, params);

	/* Set the desired hardware parameters. */

	/* Interleaved mode */
	snd_pcm_hw_params_set_access(*handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	/* Signed 16-bit little-endian format */
	snd_pcm_hw_params_set_format(*handle, params, SND_PCM_FORMAT_S32_LE);

	/* Two channels (stereo) */
	snd_pcm_hw_params_set_channels(*handle, params, 2);

	/* 48000 bits/second sampling rate */
	val = 48000;
	snd_pcm_hw_params_set_rate_near(*handle, params, &val, &exactness);

	#if 1
		if ((rc = snd_pcm_hw_params_set_buffer_size(*handle, params, period*128)) < 0) {
			fprintf(stderr, "cannot set buffer time (%s)", snd_strerror(rc));
		}
	#endif

	snd_pcm_hw_params_set_period_size_near(*handle, params, &period, &exactness);

	/* Write the parameters to the driver */
	if ((rc = snd_pcm_hw_params(*handle, params)) < 0) {
	    fprintf(stderr, "unable to set hw parameters: %s\n", snd_strerror(rc));
	    exit(1);
	}

	return period;
}

static void* realtime_audio(void* p) // no printf when audio is working please
{
	Audio_t* audio = (Audio_t*)p;
	int rc;

	audio->active = 1;

	while(audio->active)
	{
	    rc = snd_pcm_readi(audio->handle_capture, audio->buffer, audio->frames);
	    if (rc == -EPIPE)
	    {
	        /* EPIPE means overrun */
	        snd_pcm_prepare(audio->handle_capture);
	    }
	    else if(rc > 0)
	    {
	    	process_audio(audio->buffer, rc);

	        rc = snd_pcm_writei(audio->handle_playback, audio->buffer, rc);
            if (rc == -EPIPE)
            {
	            /* EPIPE means underrun */
            	snd_pcm_prepare(audio->handle_playback);
            }
	    }
	}

	return p;
}

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

int start_audio_thread(Audio_t* audio)
{
	pthread_attr_t attr;
	int err;
	int size;

	size = audio->frames * 4; /* 2 bytes/sample, 2 channels */
	if(NULL == (audio->buffer = (int*)malloc(size))) {
		fprintf(stderr, "start_audio_thread: allocation error\n");
		return 0;
	}

	if(0 != (err = pthread_attr_init(&attr))) {
		printf("pthread_attr_init: error (%s)\n", pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) { // PTHREAD_CREATE_JOINABLE
		printf("pthread_attr_setdetachstate: error (%s)\n", pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))) {
		printf("pthread_attr_setinheritsched: error (%s)\n", pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_create(&audio->realtime_audio_thread, &attr, realtime_audio, &Audio))) {
		printf("pthread_create: error (%s)\n", pthread_err(err));
		return 0;
	}

	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if(0 != (err = pthread_setschedparam(audio->realtime_audio_thread, SCHED_FIFO, &param))) {
		printf("pthread_setschedparam: error (%s)\n", pthread_err(err));
		return 0;
	}

	return 1;
}

void stop_audio(Audio_t* audio)
{
	audio->active = 0;

	// wait for thread termination
	while(0 == pthread_kill(audio->realtime_audio_thread, 0)) {
		sleep(0);
	}

	printf("finished real-time audio thread\n");

	snd_pcm_drain(audio->handle_playback);
	snd_pcm_close(audio->handle_playback);
	snd_pcm_drain(audio->handle_capture);
	snd_pcm_close(audio->handle_capture);

	free(audio->buffer);
}

static void safe_exit()
{
	stop_audio(&Audio);

	exit(1);
}

static void signal_handler(int s)
{
    printf("\nCaught signal %d\n",s);

    safe_exit();
}

uint64_t get_microseconds()
{
	uint64_t t;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	t = (uint64_t) time.tv_sec * 1000000 + (uint64_t) time.tv_nsec / 1000;
	return t;
}

int main()
{
	Audio_t* audio = &Audio;
	snd_pcm_uframes_t frames;
	uint64_t t0, t;
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = signal_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGTERM, &sigIntHandler, NULL);

	audio->frames = open_stream(&audio->handle_capture, SND_PCM_STREAM_CAPTURE, 32);
	frames = open_stream(&audio->handle_playback, SND_PCM_STREAM_PLAYBACK, 32);

	if(audio->frames != frames) {
	    fprintf(stderr, "in/out buffers are different %d != %d ", (int)audio->frames, (int)frames);
	    exit(1);
	}

	start_audio_thread(audio);

	t0 = get_microseconds();

	while (1)
	{
		t = get_microseconds();
		if(t - t0 > 1000000) {
			t0 = t;
			printf(".");
			fflush(stdout);
		}
		sleep(0);
	}

	return 0;
}
