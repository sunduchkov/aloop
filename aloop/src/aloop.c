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

#define DEFAULT_SAMPLE_RATE		48000
#define DEFAULT_PERIOD_SIZE		32				// frames (stereo samples) number between PCM interrupts
#define DEFAULT_PLAYBACK_DEVICE	"plughw:0,0"
#define DEFAULT_CAPTURE_DEVICE	"plughw:0,0"

typedef struct
{
	int* buffer;
	int  frames;
	int  active;
	snd_pcm_t *handle_playback;
	snd_pcm_t *handle_capture;
	pthread_t realtime_audio_thread;

}	Audio_t;

static Audio_t Audio; // should be global for safe_exit
static int main_thread_active; // should be global for signal_handler

static void process_audio(int* samples, int n)
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

static int open_stream(snd_pcm_t** handle, const char* device_name, int dir, snd_pcm_uframes_t period)
{
	snd_pcm_hw_params_t *params;
	unsigned int val;
	int exactness;
	int rc;

	/* Open PCM device for capture/playback */
	if ((rc = snd_pcm_open(handle, device_name, dir, SND_PCM_NONBLOCK)) < 0) {
	    fprintf(stderr, "%s: unable to open pcm device: %s\n", __FUNCTION__, snd_strerror(rc));
	    exit(EXIT_FAILURE);
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

	/* Set sampling rate */
	val = DEFAULT_SAMPLE_RATE;
	snd_pcm_hw_params_set_rate_near(*handle, params, &val, &exactness);

	snd_pcm_hw_params_set_period_size_near(*handle, params, &period, &exactness);

	/* Write the parameters to the driver */
	if ((rc = snd_pcm_hw_params(*handle, params)) < 0) {
	    fprintf(stderr, "%s: unable to set hardware parameters: %s\n", __FUNCTION__, snd_strerror(rc));
	    exit(EXIT_FAILURE);
	}

	return period;
}

static void* realtime_audio(void* p) // no printf in real-time thread please
{
	Audio_t* audio = (Audio_t*)p;
	int rc;

	memset(audio->buffer, 0, audio->frames*8);

    snd_pcm_prepare(audio->handle_capture);
	snd_pcm_prepare(audio->handle_playback);

	do {
		rc = snd_pcm_writei(audio->handle_playback, audio->buffer, audio->frames);
	}	while(rc > 0);

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

	snd_pcm_drop(audio->handle_playback);
	snd_pcm_drop(audio->handle_capture);

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

int start_audio(Audio_t* audio)
{
	pthread_attr_t attr;
	int err;
	int size;

	audio->frames = open_stream(&audio->handle_capture, DEFAULT_CAPTURE_DEVICE, SND_PCM_STREAM_CAPTURE, DEFAULT_PERIOD_SIZE);
	open_stream(&audio->handle_playback, DEFAULT_PLAYBACK_DEVICE, SND_PCM_STREAM_PLAYBACK, DEFAULT_PERIOD_SIZE);

	size = audio->frames * 8; /* 4 bytes/sample, 2 channels */
	if(NULL == (audio->buffer = (int*)malloc(size))) {
		fprintf(stderr, "%s: start_audio_thread: allocation error\n", __FUNCTION__);
		return 0;
	}

	if(0 != (err = pthread_attr_init(&attr))) {
		fprintf(stderr, "%s: pthread_attr_init: error (%s)\n", __FUNCTION__, pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
		fprintf(stderr, "%s: pthread_attr_setdetachstate: error (%s)\n", __FUNCTION__, pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))) {
		fprintf(stderr, "%s: pthread_attr_setinheritsched: error (%s)\n", __FUNCTION__, pthread_err(err));
		return 0;
	}

	if(0 != (err = pthread_create(&audio->realtime_audio_thread, &attr, realtime_audio, audio))) {
		fprintf(stderr, "%s: pthread_create: error (%s)\n", __FUNCTION__, pthread_err(err));
		return 0;
	}

	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if(0 != (err = pthread_setschedparam(audio->realtime_audio_thread, SCHED_FIFO, &param))) {
		fprintf(stderr, "%s: pthread_setschedparam: error (%s)\n", __FUNCTION__, pthread_err(err));
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

	printf("Finished real-time audio thread\n");

	if(audio->handle_playback) {
		snd_pcm_hw_free(audio->handle_playback);
		snd_pcm_close(audio->handle_playback);
		audio->handle_playback = NULL;
	}

	if(audio->handle_capture) {
		snd_pcm_hw_free(audio->handle_capture);
		snd_pcm_close(audio->handle_capture);
		audio->handle_capture = NULL;
	}

	if(audio->buffer) {
		free(audio->buffer);
		audio->buffer = NULL;
	}
}

static void safe_exit()
{
    printf("Safe exit\n");
	stop_audio(&Audio);
}

static void signal_handler(int s)
{
    printf("\nCaught signal %d\n",s);
    main_thread_active = 0;
}

int main()
{
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = signal_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGTERM, &sigIntHandler, NULL);

	atexit(safe_exit);

	if(!start_audio(&Audio)) {
		fprintf(stderr, "%s: can't start audio\n", __FUNCTION__);
	    exit(EXIT_FAILURE);
	}

    main_thread_active = 1;

	while(main_thread_active)
	{
		printf(".");
		fflush(stdout);
		sleep(1);
	}

	printf("Finished main audio thread\n");

	return 0;
}
