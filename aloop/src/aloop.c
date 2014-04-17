/*
 *	Audio path-through
 *
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.c
 *
 */

#include <math.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

//
// ALSA terminology
//
// One frame might contain one sample (when only one converter is used - mono)
// or more samples (for example: stereo has signals from two converters recorded at same time).
// Digital audio stream contains collection of frames recorded at boundaries of continuous time periods.
//
// ALSA uses the ring buffer to store outgoing (playback) and incoming (capture, record) samples.
// There are two pointers being maintained to allow a precise communication between application and
// device pointing to current processed sample by hardware and last processed sample by application.
// The modern audio chips allow to program the transfer time periods. It means that the stream of samples
// is divided to small chunks. Device acknowledges to application when the transfer of a chunk is complete.
//

// parameters

#define SAMPLERATE				48000
#define NCHANNELS				2				// stereo signal, two samples in one frame
#define PERIOD_SIZE				1440//512//360		// frames number between PCM interrupts
#define BUFFER_SIZE				(2*PERIOD_SIZE)	// ring buffer in frames. should be at least 2 periods

#define CAPTURE_ENABLED			1
#define MMAP_ACCESS_ENABLED		0
#define POLLING_ENABLED			0
#define	USING_SYSTEM_POLL		1

// calculated constants

#define PERIOD_TIME				(PERIOD_SIZE * 1000 / SAMPLERATE) // [ms] between PCM interrupts
#define BUFFER_TIME				(BUFFER_SIZE * 1000 / SAMPLERATE)
#define LATENCY					BUFFER_TIME

#define AVAIL_READ_MIN			(PERIOD_SIZE/2)//1024
#define AVAIL_WRITE_MIN			(PERIOD_SIZE/2)//1024

#if MMAP_ACCESS_ENABLED
typedef void (*process_t)(int* pSamplesIn[NCHANNELS], int* pSamplesOut[NCHANNELS], int nLength);
#else
typedef void (*process_t)(int* pSamplesIn, int* pSamplesOut, int nLength);
#endif

typedef struct _alsa_driver
{
    snd_pcm_t                    *playback_handle;
    snd_pcm_t                    *capture_handle;
    snd_ctl_t                    *ctl_handle;

    char                         *alsa_driver_name;
    char                         *alsa_name_playback;
    char                         *alsa_name_capture;

#if MMAP_ACCESS_ENABLED
    int 						 *capture_addr[NCHANNELS];
    int 						 *playback_addr[NCHANNELS];
    unsigned long                 capture_interleave_skip[NCHANNELS];
    unsigned long                 playback_interleave_skip[NCHANNELS];
#else
    int 						  samples[BUFFER_SIZE*NCHANNELS];
#endif

#if USING_SYSTEM_POLL
    struct pollfd                *pfd;
    unsigned int                  playback_nfds;
    unsigned int                  capture_nfds;
#endif

    int                           capture_and_playback_not_synced;
	int                      	  poll_timeout;

}	alsa_driver_t;

uint64_t
get_microseconds_from_system (void) {
	uint64_t t;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	t = (uint64_t) time.tv_sec * 1000000 + (uint64_t) time.tv_nsec / 1000;
	return t;
}

#if MMAP_ACCESS_ENABLED
static void ProcessStereo(int* samplesIn[NCHANNELS], int* samplesOut[NCHANNELS], int N)
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
		L = samplesIn[i*NCHANNELS];
		R = samplesIn[i*NCHANNELS+1];
#else
		// Square signal to Left channel
		if(i & 0x40) L = (1<<30); else L = -(1<<30);
		R = L;
#endif

		// Spectrum inversion for Right channel
		//if(i & 1) R = -R;

		samplesOut[i*NCHANNELS] = L;
		samplesOut[i*NCHANNELS+1] = R;
	}
}
#endif

static int open_stream(snd_pcm_t **handle, const char *name, int dir)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	unsigned int rate = SAMPLERATE;
	unsigned int format = SND_PCM_FORMAT_S32_LE;
	snd_pcm_uframes_t buffer_size = BUFFER_SIZE;
	snd_pcm_uframes_t period_size = PERIOD_SIZE;
	int err;

	if ((err = snd_pcm_open(handle, name, dir, SND_PCM_NONBLOCK)) < 0) {
		fprintf(stderr, "%s (%s): cannot open audio device (%s) in SND_PCM_NONBLOCK mode\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_any(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize hardware parameter structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

#if MMAP_ACCESS_ENABLED
	if ((err = snd_pcm_hw_params_set_access(*handle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED  )) < 0) {
		fprintf(stderr, "%s (%s): cannot set access type(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
#else
	if ((err = snd_pcm_hw_params_set_access(*handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf(stderr, "%s (%s): cannot set access type(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
#endif

	if ((err = snd_pcm_hw_params_set_format(*handle, hw_params, format)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample format(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_buffer_size_near(*handle, hw_params, &buffer_size)) < 0) {
		fprintf(stderr, "%s (%s): cannot set buffer time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual buffer size %d = %d [ms]\n", (int)buffer_size, (int)buffer_size*1000/SAMPLERATE);

	if ((err = snd_pcm_hw_params_set_period_size_near(*handle, hw_params, &period_size, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual period size %d = %d [ms]\n", (int)period_size, (int)period_size*1000/SAMPLERATE);

	if ((err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &rate, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual sample rate %d\n", rate);

	if ((err = snd_pcm_hw_params_set_channels(*handle, hw_params, NCHANNELS)) < 0) {
		fprintf(stderr, "%s (%s): cannot set channel count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params(*handle, hw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_sw_params_malloc(&sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot allocate software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_current(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot initialize software parameters structure(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, BUFFER_SIZE/2)) < 0) {
		fprintf(stderr, "%s (%s): cannot set minimum available count(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params_set_start_threshold(*handle, sw_params, 0U)) < 0) {
		fprintf(stderr, "%s (%s): cannot set start mode(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	if ((err = snd_pcm_sw_params(*handle, sw_params)) < 0) {
		fprintf(stderr, "%s (%s): cannot set software parameters(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	snd_pcm_sw_params_free(sw_params);

	return 0;
}

static char*
get_control_device_name(const char * device_name)
{
    char * ctl_name;
    const char * comma;

    /* the user wants a hw or plughw device, the ctl name
     * should be hw:x where x is the card identification.
     * We skip the subdevice suffix that starts with comma */

    if (strncasecmp(device_name, "plughw:", 7) == 0) {
        /* skip the "plug" prefix" */
        device_name += 4;
    }

    comma = strchr(device_name, ',');
    if (comma == NULL) {
        ctl_name = strdup(device_name);
        if (ctl_name == NULL) {
            printf("strdup(\"%s\") failed.", device_name);
        }
    } else {
        ctl_name = strndup(device_name, comma - device_name);
        if (ctl_name == NULL) {
        	printf("strndup(\"%s\", %u) failed.", device_name, (unsigned int)(comma - device_name));
        }
    }

    return ctl_name;
}

alsa_driver_t* alsa_driver_new()
{
	alsa_driver_t* driver;
	snd_ctl_card_info_t *card_info;
	char * ctl_name;
	int err;
	uint64_t period_usecs;

	driver = (alsa_driver_t *) calloc (1, sizeof (alsa_driver_t));

	driver->playback_handle = NULL;
	driver->capture_handle = NULL;

	driver->alsa_name_playback = strdup ("hw:0,0");
	driver->alsa_name_capture = strdup ("hw:0,0");

	snd_ctl_card_info_alloca (&card_info);

	ctl_name = get_control_device_name(driver->alsa_name_playback);

	// XXX: I don't know the "right" way to do this. Which to use
	// driver->alsa_name_playback or driver->alsa_name_capture.
	if ((err = snd_ctl_open (&driver->ctl_handle, ctl_name, 0)) < 0) {
		printf ("control open \"%s\" (%s)", ctl_name,
			    snd_strerror(err));
	} else if ((err = snd_ctl_card_info(driver->ctl_handle, card_info)) < 0) {
		printf ("control hardware info \"%s\" (%s)",
			    driver->alsa_name_playback, snd_strerror (err));
		snd_ctl_close (driver->ctl_handle);
	}

	driver->alsa_driver_name = strdup(snd_ctl_card_info_get_driver (card_info));

	free(ctl_name);


	if ((err = open_stream(&driver->playback_handle, driver->alsa_name_playback, SND_PCM_STREAM_PLAYBACK)) < 0)
		return NULL;
	printf("Playback opened\n");

#if CAPTURE_ENABLED
	if ((err = open_stream(&driver->capture_handle, driver->alsa_name_capture, SND_PCM_STREAM_CAPTURE)) < 0)
		return NULL;
	printf("Capture opened\n");
#endif

	driver->capture_and_playback_not_synced = 0;

	if (driver->capture_handle && driver->playback_handle) {
		if (snd_pcm_link (driver->playback_handle,
				  driver->capture_handle) != 0) {
			driver->capture_and_playback_not_synced = 1;
		}
	}

	period_usecs = (uint64_t) floor ((((float) PERIOD_SIZE) / SAMPLERATE) * 1000000.0f);
	driver->poll_timeout = (int) floor (1.5f * period_usecs /1000.0);

#if POLLING_ENABLED
	printf("polling timeout %d [ms]\n", driver->poll_timeout);
#endif

	if(0 == driver->capture_and_playback_not_synced)
	{
		printf("Playback and Capture are synced\n");
	}

	return driver;
}

#if MMAP_ACCESS_ENABLED
static int get_channel_address(
		snd_pcm_t* handle,
		snd_pcm_uframes_t* offset, snd_pcm_uframes_t* avail,
		char* addr[NCHANNELS], unsigned long interleave_skip[NCHANNELS])
{
	const snd_pcm_channel_area_t* areas;
	int err;
	int chn;

	if ((err = snd_pcm_mmap_begin (handle, &areas, offset, avail)) < 0) {
		return err;
	}

	for (chn = 0; chn < NCHANNELS; chn++) {
		const snd_pcm_channel_area_t *a = &areas[chn];
		addr[chn] = (char *) a->addr + ((a->first + a->step * *(offset)) / 8);
		interleave_skip[chn] = (unsigned long) (a->step / 8);
		//printf("first %d, step %d\n", a->first, a->step);
	}

	return 0;
}
#endif

int alsa_driver_prepare(alsa_driver_t* driver)
{
	int err;

#if MMAP_ACCESS_ENABLED
    char* playback_addr[NCHANNELS];
	snd_pcm_uframes_t avail;
	snd_pcm_uframes_t offset;
#endif

	if (driver->playback_handle) {
		if ((err = snd_pcm_prepare (driver->playback_handle)) < 0) {
			printf ("ALSA: prepare error for playback on "
				    "\"%s\" (%s)", driver->alsa_name_playback,
				    snd_strerror(err));
			return 0;
		}
	}

	if ((driver->capture_handle && driver->capture_and_playback_not_synced)
	    || !driver->playback_handle) {
		if ((err = snd_pcm_prepare (driver->capture_handle)) < 0) {
			printf ("ALSA: prepare error for capture on \"%s\""
				    " (%s)", driver->alsa_name_capture,
				    snd_strerror(err));
			return 0;
		}
	}

#if USING_SYSTEM_POLL
	if (driver->playback_handle) {
		driver->playback_nfds =
			snd_pcm_poll_descriptors_count (driver->playback_handle);
	} else {
		driver->playback_nfds = 0;
	}

	if (driver->capture_handle) {
		driver->capture_nfds =
			snd_pcm_poll_descriptors_count (driver->capture_handle);
	} else {
		driver->capture_nfds = 0;
	}

	if (driver->pfd) {
		free (driver->pfd);
	}

	driver->pfd = (struct pollfd *)
		malloc (sizeof (struct pollfd) *
			(driver->playback_nfds + driver->capture_nfds + 2));
#endif

#if MMAP_ACCESS_ENABLED
	avail = snd_pcm_avail_update (driver->playback_handle);

	if (avail != BUFFER_SIZE) {
		printf ("ALSA: full buffer not available at start, %u\n", (unsigned int)avail);
		return -1;
	}

	if ((err = get_channel_address(
			driver->playback_handle,
			&offset, &avail, playback_addr, driver->playback_interleave_skip) < 0)) {
		printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_playback);
		return -1;
	}

	if ((err = snd_pcm_mmap_commit (driver->playback_handle, offset, avail)) < 0) {
		printf ("ALSA: could not complete playback of %u frames: error = %d", (unsigned int)avail, err);
		if (err != -EPIPE && err != -ESTRPIPE)
			return -1;
	}

	if ((err = snd_pcm_start (driver->playback_handle)) < 0) {
		fprintf(stderr, "cannot start audio interface for playback (%s)\n",
			 snd_strerror(err));
		return 0;
	}
#endif
	return 1;
}

int alsa_driver_start(alsa_driver_t* driver)
{
#if MMAP_ACCESS_ENABLED
    char* playback_addr[NCHANNELS];
	snd_pcm_uframes_t offset;
#endif
	snd_pcm_uframes_t avail;
	int err;

	avail = snd_pcm_avail_update (driver->playback_handle);

	if (avail != BUFFER_SIZE) {
		printf ("ALSA: full buffer not available at start, %u\n", (unsigned int)avail);
		return -1;
	}
#if MMAP_ACCESS_ENABLED
	if ((err = get_channel_address(
			driver->playback_handle,
			&offset, &avail, playback_addr, driver->playback_interleave_skip) < 0)) {
		printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_playback);
		return -1;
	}

	if ((err = snd_pcm_mmap_commit (driver->playback_handle, offset, avail)) < 0) {
		printf ("ALSA: could not complete playback of %u frames: error = %d", (unsigned int)avail, err);
		if (err != -EPIPE && err != -ESTRPIPE)
			return -1;
	}

	if ((err = snd_pcm_start (driver->playback_handle)) < 0) {
		fprintf(stderr, "cannot start audio interface for playback (%s)\n",
			 snd_strerror(err));
		return 0;
	}
#else
	memset (driver->samples, 0, BUFFER_SIZE*NCHANNELS*sizeof(int));

	if ((err = snd_pcm_writei(driver->playback_handle, driver->samples, avail)) < 0) {
		printf ("write failed %s\n", snd_strerror (err));
		exit (1);
	}
#endif
	return 1;
}

int alsa_driver_wait(alsa_driver_t* driver)
{
	int ret;

#if USING_SYSTEM_POLL
	int i;

	int nfds = 0;
	int ci;
	uint64_t poll_start, poll_end;

	if (driver->playback_handle) {
		snd_pcm_poll_descriptors (driver->playback_handle,
					  &driver->pfd[0],
					  driver->playback_nfds);
		nfds += driver->playback_nfds;
	}

	if (driver->capture_handle) {
		snd_pcm_poll_descriptors (driver->capture_handle,
					  &driver->pfd[nfds],
					  driver->capture_nfds);
		ci = nfds;
		nfds += driver->capture_nfds;
	}

	/* ALSA doesn't set POLLERR in some versions of 0.9.X */

	for (i = 0; i < nfds; i++) {
		driver->pfd[i].events |= POLLERR;
	}

	poll_start = get_microseconds_from_system();

	int poll_result = poll (driver->pfd, nfds, driver->poll_timeout);
	if (poll_result < 0) {

		if (errno == EINTR) {
			printf ("poll interrupt");
			return 0;
		}

		printf ("ALSA: poll call failed (%s)", strerror (errno));
		return 0;
	}

	poll_end = get_microseconds_from_system();
	ret = (int)((poll_end - poll_start) / 1000);

	unsigned short revents;

	if (driver->playback_handle)
	{
		if (snd_pcm_poll_descriptors_revents
		    (driver->playback_handle, &driver->pfd[0],
		     driver->playback_nfds, &revents) < 0)
		{
			printf ("ALSA: playback revents failed\n");
			return 0;
		}

		if (revents & POLLERR)
		{
			printf ("playback xrun\n");
		}
	}

	if (driver->capture_handle)
	{
		if (snd_pcm_poll_descriptors_revents
		    (driver->capture_handle, &driver->pfd[ci],
		     driver->capture_nfds, &revents) < 0) {
			printf ("ALSA: capture revents failed\n");
			return 0;
		}

		if (revents & POLLERR)
		{
			printf ("capture xrun\n");
		}
	}
#else
	if ((ret = snd_pcm_wait (driver->playback_handle, driver->poll_timeout)) == 0) {
			printf ("PCM wait failed, driver timeout\n");
	}
#endif
	return ret;
}

int alsa_driver_write(alsa_driver_t* driver, process_t process)
{
#if MMAP_ACCESS_ENABLED
	snd_pcm_uframes_t offset;
#endif
	snd_pcm_uframes_t avail;
	int ret;
	int err;

	ret = 0;

	if(driver->playback_handle)
	{
		avail = snd_pcm_avail_update(driver->playback_handle);
		if (avail >= AVAIL_WRITE_MIN)
		{
			if (avail > BUFFER_SIZE)
				avail = BUFFER_SIZE;

#if MMAP_ACCESS_ENABLED
			if ((err = get_channel_address(
					driver->playback_handle,
					&offset, &avail, (char**)driver->playback_addr, driver->playback_interleave_skip) < 0)) {
				printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_playback);
				return -1;
			}

			process(driver->capture_addr, driver->playback_addr, avail);

			if ((err = snd_pcm_mmap_commit (driver->playback_handle, offset, avail)) < 0) {
				printf ("ALSA: could not complete playback of %u frames: error = %d", (unsigned int)avail, err);
				if (err != -EPIPE && err != -ESTRPIPE)
					return -1;
			}
#else
			process(driver->samples, driver->samples, avail);

			if ((err = snd_pcm_writei(driver->playback_handle, driver->samples, avail)) < 0) {
				printf ("write failed %s\n", snd_strerror (err));
				exit(1);
			}
#endif

			ret = avail;

		} else {
				//printf ("nothing to write\n");
		}
	}

	return ret;
}

int alsa_driver_read(alsa_driver_t* driver)
{
#if MMAP_ACCESS_ENABLED
	snd_pcm_uframes_t offset;
	int err;
#else
	int actual;
#endif
	snd_pcm_uframes_t avail;
	int ret;

	ret = 0;

	if (driver->capture_handle)
	{
		avail = snd_pcm_avail_update(driver->capture_handle);
		if (avail >= AVAIL_READ_MIN)
		{
			if (avail > BUFFER_SIZE)
				avail = BUFFER_SIZE;

#if MMAP_ACCESS_ENABLED
			if ((err = get_channel_address(
					driver->capture_handle,
					&offset, &avail, (char**)driver->capture_addr, driver->capture_interleave_skip) < 0)) {
				printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_capture);
				return -1;
			}

			if ((err = snd_pcm_mmap_commit (driver->capture_handle, offset, avail)) < 0) {
				printf ("ALSA: could not complete read of %u frames: error = %d", (unsigned int)avail, err);
				return -1;
			}
#else
			if ((actual = snd_pcm_readi(driver->capture_handle, driver->samples, avail)) < 0) {
				printf ("read failed %s\n", snd_strerror (actual));
				exit(1);
			}
#endif
			ret = avail;

		} else {
				//printf ("nothing to read\n");
		}
	}

	return ret;
}

void setscheduler(void)
{
	struct sched_param sched_param;

	if (sched_getparam(0, &sched_param) < 0) {
		printf("Scheduler getparam failed...\n");
		return;
	}
	sched_param.sched_priority = sched_get_priority_max(SCHED_RR) - 10;
	if (!sched_setscheduler(0, SCHED_RR, &sched_param)) {
		printf("Scheduler set to Round Robin with priority %i...\n", sched_param.sched_priority);
		fflush(stdout);
		return;
	}
	printf("!!!Scheduler set to Round Robin with priority %i FAILED!!!\n", sched_param.sched_priority);
}

int main(int argc, char *argv[])
{
	alsa_driver_t* driver;

	int r, w;
	int avail;

	printf("ALSA Path-through starting\n");

	//setscheduler ();

	if(NULL == (driver = alsa_driver_new()))
	{
		printf("alsa_driver_new not succeeded\n");
		return 0;
	}

	printf("Audio Interface \"%s\" initialized with %d [ms] latency\n", driver->alsa_driver_name, LATENCY);

	if(!alsa_driver_prepare(driver))
	{
		printf("alsa_driver_start not succeeded\n");
		return 0;
	}

	printf("Audio Interface prepared for start\n");

	alsa_driver_start(driver);

	printf("Audio started\n");

	r = 0;
	w = 0;

	while (1)
	{
#if POLLING_ENABLED
		int poll_time = alsa_driver_wait(driver);
		printf("poll %d, DSP usage %d%%\n",poll_time, (int)(100*(1-poll_time/(float)PERIOD_TIME)));
#endif

#if CAPTURE_ENABLED
		avail = alsa_driver_read(driver);
		r += avail;
		if(r >= SAMPLERATE)
		{
			printf(".");
			fflush(stdout);
			r = 0;
		}
#endif

		avail = alsa_driver_write(driver, ProcessStereo);

		w += avail;
		if(w >= SAMPLERATE)
		{
			printf("+");
			fflush(stdout);
			w = 0;
		}

	}

	return 0;
}
