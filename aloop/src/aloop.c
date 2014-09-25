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

#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>

#define DEFAULT_SAMPLE_RATE		48000

// frames (stereo samples) number between PCM interrupts
#define DEFAULT_PERIOD_SIZE		32

#define DEFAULT_BUFFER_SIZE		(128*DEFAULT_PERIOD_SIZE)

// hw: Direct hardware device without any conversions
// plughw: Hardware device with all software conversions
// some boards are not supporting a lot of sample rates by hardware
// in this case software conversion can help
#define DEFAULT_DEVICE	"plughw:0,0"

typedef struct
{
	int* buffer;
	int  active;
	int  corrupted;
	snd_pcm_t *handle_playback;
	snd_pcm_t *handle_capture;
	pthread_t realtime_audio_thread;

	// parameters
	unsigned int sample_rate;
	char* device_name;
	int period_size;
	int buffer_size;

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

static void* realtime_audio(void* p) // no printf in real-time thread please
{
	Audio_t* audio = (Audio_t*)p;
	snd_pcm_sframes_t avail;
	int rc;

    snd_pcm_prepare(audio->handle_capture);
	snd_pcm_prepare(audio->handle_playback);

	avail = snd_pcm_avail_update (audio->handle_playback);

	snd_pcm_writei(audio->handle_playback, audio->buffer, avail);

	audio->active = 1;
	audio->corrupted = 0;

	while(audio->active)
	{
	    rc = snd_pcm_readi(audio->handle_capture, audio->buffer, audio->period_size);
	    if (rc == -EPIPE)
	    {
	        /* EPIPE means overrun */
	    	audio->corrupted = 1;
	        snd_pcm_prepare(audio->handle_capture);
	    }
	    else if(rc > 0)
	    {
	    	process_audio(audio->buffer, rc);

	        rc = snd_pcm_writei(audio->handle_playback, audio->buffer, rc);
            if (rc == -EPIPE)
            {
	            /* EPIPE means underrun */
    	    	audio->corrupted = 1;
            	snd_pcm_prepare(audio->handle_playback);
            }
	    }

	    usleep(0);
	}

	snd_pcm_drop(audio->handle_playback);
	snd_pcm_drop(audio->handle_capture);

	return p;
}

static int open_stream(snd_pcm_t** handle, const char* device_name, int dir, unsigned int* sample_rate, snd_pcm_uframes_t period, snd_pcm_uframes_t buffer_size)
{
	snd_pcm_hw_params_t *params;
	int exactness;
	int err;

	/* Open PCM device for capture/playback */
	if ((err = snd_pcm_open(handle, device_name, dir, SND_PCM_NONBLOCK)) < 0) {
	    fprintf(stderr, "%s: unable to open pcm device: %s\n", __FUNCTION__, snd_strerror(err));
	    exit(EXIT_FAILURE);
	}

	/* Allocate a hardware parameters object. */
	snd_pcm_hw_params_alloca(&params);

	/* Fill it in with default values. */
	snd_pcm_hw_params_any(*handle, params);

	/* Set the desired hardware parameters. */

	/* Interleaved mode */
	snd_pcm_hw_params_set_access(*handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

	/* Signed 32-bit little-endian format */
	snd_pcm_hw_params_set_format(*handle, params, SND_PCM_FORMAT_S32_LE);

	/* Two channels (stereo) */
	snd_pcm_hw_params_set_channels(*handle, params, 2);

	if ((err = snd_pcm_hw_params_set_period_size(*handle, params, period, 0)) < 0) {
		fprintf(stderr, "%s: cannot set period size (%s)\n", __FUNCTION__, snd_strerror(err));
	    exit(EXIT_FAILURE);
	}

	if ((err = snd_pcm_hw_params_set_buffer_size(*handle, params, buffer_size)) < 0) {
		fprintf(stderr, "%s: cannot set buffer size (%s)\n", __FUNCTION__, snd_strerror(err));
	    exit(EXIT_FAILURE);
	}

	/* Set sampling rate */
	if ((err = snd_pcm_hw_params_set_rate_near(*handle, params, sample_rate, &exactness)) < 0) {
		fprintf(stderr, "%s: cannot set sample rate (%s)", __FUNCTION__, snd_strerror(err));
	    exit(EXIT_FAILURE);
	}
	printf("%s: sample rate %d\n", __FUNCTION__, *sample_rate);

	/* Write the parameters to the driver */
	if ((err = snd_pcm_hw_params(*handle, params)) < 0) {
	    fprintf(stderr, "%s: unable to set hardware parameters: %s\n", __FUNCTION__, snd_strerror(err));
	    exit(EXIT_FAILURE);
	}

	return 1;
}

int start_audio(Audio_t* audio)
{
	pthread_attr_t attr;
	int err;

	open_stream(&audio->handle_capture, audio->device_name, SND_PCM_STREAM_CAPTURE, &audio->sample_rate, audio->period_size, audio->buffer_size);
	open_stream(&audio->handle_playback, audio->device_name, SND_PCM_STREAM_PLAYBACK, &audio->sample_rate, audio->period_size, audio->buffer_size);

	/* 4 bytes/sample, 2 channels */
	if(NULL == (audio->buffer = (int*)malloc(audio->period_size * 8))) {
		fprintf(stderr, "%s: start_audio_thread: allocation error\n", __FUNCTION__);
		return 0;
	}
	memset(audio->buffer, 0, audio->period_size * 8);

	if(0 != (err = pthread_attr_init(&attr))) {
		fprintf(stderr, "%s: pthread_attr_init: error %d\n", __FUNCTION__, err);
		return 0;
	}

	if(0 != (err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))) {
		fprintf(stderr, "%s: pthread_attr_setdetachstate: error %d\n", __FUNCTION__, err);
		return 0;
	}

	if(0 != (err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED))) {
		fprintf(stderr, "%s: pthread_attr_setinheritsched: error %d\n", __FUNCTION__, err);
		return 0;
	}

	if(0 != (err = pthread_create(&audio->realtime_audio_thread, &attr, realtime_audio, audio))) {
		fprintf(stderr, "%s: pthread_create: error %d\n", __FUNCTION__, err);
		return 0;
	}

	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 10;
	if(0 != (err = pthread_setschedparam(audio->realtime_audio_thread, SCHED_FIFO, &param))) {
		fprintf(stderr, "%s: pthread_setschedparam: error %d\n", __FUNCTION__, err);
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

static int get_options(Audio_t* audio, int argc, char *argv[])
{
	//alsa_driver_t* driver;

	int needhelp = 0;
	const struct option long_option[] =
	{
		{"help", no_argument, NULL, 'h'},
		{"device", required_argument, NULL, 'd'},
		{"rate", required_argument, NULL, 'r'},
		{"period", required_argument, NULL, 'p'},
		{"buffer", required_argument, NULL, 'b'},
		{NULL, 0, NULL, 0},
	};

	int c;
	int err;

	if(!audio) {
		printf("main_get_options with NULL pointer");
		return 0;
	}

	audio->sample_rate = DEFAULT_SAMPLE_RATE;
	audio->period_size = DEFAULT_PERIOD_SIZE;
	audio->buffer_size = DEFAULT_BUFFER_SIZE;
	audio->device_name = strdup(DEFAULT_DEVICE);

	while ((c = getopt_long(argc, argv, "hd:r:p:b:", long_option, NULL)) != -1) {
		switch (c) {
		case 'h':
			needhelp = 1;
			break;
		case 'd':
			audio->device_name = strdup(optarg);
			break;
		case 'r':
			err = atoi(optarg);
			audio->sample_rate = err;
			break;
		case 'p':
			err = atoi(optarg);
			audio->period_size = err;
			break;
		case 'b':
			err = atoi(optarg);
			audio->buffer_size = err;
			break;
		}
	}

	if (needhelp) {
		printf(
				"Usage: aloop [OPTIONS]\n"
				"-h,--help      this message\n"
				"-d,--device    playback/capture device (hw:0,0 by default - good for internal devices, try hw:1,0 for others)\n"
				"-r,--rate      sample rate in [Hz]\n"
				"-p,--period    period size in frames\n"
				"-b,--buffer    buffer size in frames (try 2 x period size first)\n"
		);
		return 0;
	}

	return 1;
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

int main(int argc, char* argv[])
{
	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = signal_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	sigaction(SIGINT, &sigIntHandler, NULL);
	sigaction(SIGTERM, &sigIntHandler, NULL);

	atexit(safe_exit);

	if(!get_options(&Audio, argc, argv)) {
		fprintf(stderr, "%s: can't get options\n", __FUNCTION__);
	    exit(EXIT_FAILURE);
	}

	printf("Start audio device %s with %d sample rate, %d period size, %d buffer size\n",
			Audio.device_name, Audio.sample_rate, Audio.period_size, Audio.buffer_size);

	if(!start_audio(&Audio)) {
		fprintf(stderr, "%s: can't start audio\n", __FUNCTION__);
	    exit(EXIT_FAILURE);
	}

    main_thread_active = 1;

	while(main_thread_active)
	{
		if(Audio.corrupted) {
			Audio.corrupted = 0;
			printf("!");
		}

		printf(".");
		fflush(stdout);
		sleep(1);
	}

	printf("Finished main audio thread\n");

	return 0;
}
