/*
 *	Audio path-through
 *
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.c
 *
 *	Beagleboard_xM, Ubuntu 13.10
 *
 *		PERIOD_TIME 		40
 *		CAPTURE_ENABLED 	1
 *		POLLING_ENABLED 	0
 *		USING_SYSTEM_POLL 	1
 *
 *	BeagleBone Black, Ubuntu 13.10
 *
 *		PERIOD_TIME 		20
 *		CAPTURE_ENABLED 	0
 *		POLLING_ENABLED 	1
 *		USING_SYSTEM_POLL 	1
 */

#include <math.h>
#include <stdio.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

// parameters

#define SAMPLERATE				48000
#define PERIOD_TIME				40		// PCM interrupt period [ms]

#define MMAP_ACCESS_ENABLED		0
#define CAPTURE_ENABLED			1
#define POLLING_ENABLED			0
#define	USING_SYSTEM_POLL		1

// calculated constants

#define BUFFER_TIME				(2*PERIOD_TIME)                     // Buffer time [ms], should be at least 2xPERIOD_TIME
#define BUFFER_SIZE_IN_PERIODS	(SAMPLERATE * BUFFER_TIME / 1000)   // sample rate [1/sec] x time [sec]
#define	LATENCY					(1000 * BUFFER_SIZE_IN_PERIODS / SAMPLERATE)   // same as BUFFER_TIME [ms]
#define PERIOD_SIZE 			(SAMPLERATE/1000*PERIOD_TIME)

#if MMAP_ACCESS_ENABLED
typedef void (*process_t)(int* pSamplesIn[2], int nOffset, int* pSamplesOut[2], int nLength);
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
    unsigned long                 capture_interleave_skip[2];
    unsigned long                 playback_interleave_skip[2];
#else
    int 						  samples[BUFFER_SIZE_IN_PERIODS*2]; // 2 for stereo
#endif

#if USING_SYSTEM_POLL
    struct pollfd                *pfd;
    unsigned int                  playback_nfds;
    unsigned int                  capture_nfds;
#endif

    int                           capture_and_playback_not_synced;
	int                      	  poll_timeout;

}	alsa_driver_t;

typedef struct
{
#if MMAP_ACCESS_ENABLED
	int*	samples[2];
#else
	int*	samples;
#endif
	int		size_in_periods;
	int		r_in_periods;
	int		w_in_periods;

}	buffer_t;

uint64_t
get_microseconds_from_system (void) {
	uint64_t t;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	t = (uint64_t) time.tv_sec * 1000000 + (uint64_t) time.tv_nsec / 1000;
	return t;
}

#if MMAP_ACCESS_ENABLED
static void ProcessStereo(int* samplesIn[2], int offset, int* samplesOut[2], int N)
{
	int i;
	int L, R;
	//static int k = 0;

	for(i = 0; i < N; ++i)
	{
#if CAPTURE_ENABLED
		L = samplesIn[0][offset + i];
		R = samplesIn[1][offset + i];
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

static int open_stream(snd_pcm_t **handle, const char *name, int dir)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	int err;
	unsigned int rate = SAMPLERATE;
	unsigned int format = SND_PCM_FORMAT_S32_LE;
	unsigned int buffer_time = BUFFER_TIME*1000; // time in usec
	unsigned int period_time = PERIOD_TIME*1000; // time in usec

	//snd_pcm_uframes_t period_size, buffer_size;

	if ((err = snd_pcm_open(handle, name, dir, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot open audio device (%s)\n",
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

	if ((err = snd_pcm_hw_params_set_buffer_time_near(*handle, hw_params, &buffer_time, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set buffer time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual buffer time %d\n", buffer_time/1000);

	if ((err = snd_pcm_hw_params_set_period_time_near(*handle, hw_params, &period_time, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual period time %d\n", period_time/1000);

	if ((err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &rate, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual sample rate %d\n", rate);

	if ((err = snd_pcm_hw_params_set_channels(*handle, hw_params, 2)) < 0) {
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
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, BUFFER_SIZE_IN_PERIODS)) < 0) {
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
		char* addr[2], unsigned long interleave_skip[2])
{
	const snd_pcm_channel_area_t* areas;
	int err;
	int chn;

	if ((err = snd_pcm_mmap_begin (handle, &areas, offset, avail)) < 0) {
		return err;
	}

	for (chn = 0; chn < 2; chn++) {
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
    char* playback_addr[2];
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

	if (avail != BUFFER_SIZE_IN_PERIODS) {
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

int alsa_driver_write(alsa_driver_t* driver, process_t process, buffer_t* buf)
{
	int ret;
	int err;

	snd_pcm_uframes_t avail;
#if MMAP_ACCESS_ENABLED
    char* playback_addr[2];
	snd_pcm_uframes_t offset;
#endif

	ret = 0;

	if(driver->playback_handle)
	{
		avail = snd_pcm_avail_update(driver->playback_handle);
		if (avail >= 1024)
		{
			if (avail > BUFFER_SIZE_IN_PERIODS)
				avail = BUFFER_SIZE_IN_PERIODS;

#if MMAP_ACCESS_ENABLED
			if ((err = get_channel_address(
					driver->playback_handle,
					&offset, &avail, playback_addr, driver->playback_interleave_skip) < 0)) {
				printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_playback);
				return -1;
			}

			process(buf->samples, buf->r_in_periods, (int**)playback_addr, avail);

			if ((err = snd_pcm_mmap_commit (driver->playback_handle, offset, avail)) < 0) {
				printf ("ALSA: could not complete playback of %u frames: error = %d", (unsigned int)avail, err);
				if (err != -EPIPE && err != -ESTRPIPE)
					return -1;
			}
#else
			process(&buf->samples[buf->r_in_periods*2], driver->samples, avail);

			if ((err = snd_pcm_writei(driver->playback_handle, driver->samples, avail)) < 0) {
				printf ("write failed %s\n", snd_strerror (err));
				exit (1);
			}

#endif
			if((buf->r_in_periods += avail) >= buf->size_in_periods) {
				buf->r_in_periods -= buf->size_in_periods;
			}

			ret = avail;

		} else {
				//printf ("nothing to write\n");
		}
	}

	return ret;
}

int alsa_driver_read(alsa_driver_t* driver, buffer_t* buf)
{
#if MMAP_ACCESS_ENABLED
    char* capture_addr[2];
	snd_pcm_uframes_t offset;
	int err;
#else
	int actual;
#endif
	int ret;

	snd_pcm_uframes_t avail;

	ret = 0;

	if (driver->capture_handle)
	{
		avail = snd_pcm_avail_update(driver->capture_handle);
		if (avail >= 1024)
		{
			if (avail > BUFFER_SIZE_IN_PERIODS)
				avail = BUFFER_SIZE_IN_PERIODS;

#if MMAP_ACCESS_ENABLED
			if ((err = get_channel_address(
					driver->capture_handle,
					&offset, &avail, capture_addr, driver->capture_interleave_skip) < 0)) {
				printf ("ALSA: %s: mmap areas info error ", driver->alsa_name_capture);
				return -1;
			}

			memcpy(&buf->samples[0][buf->w_in_periods], capture_addr[0], avail*sizeof(int));
			memcpy(&buf->samples[1][buf->w_in_periods], capture_addr[1], avail*sizeof(int));

			if ((err = snd_pcm_mmap_commit (driver->capture_handle, offset, avail)) < 0) {
				printf ("ALSA: could not complete read of %u frames: error = %d", (unsigned int)avail, err);
				return -1;
			}

			if((buf->w_in_periods += avail) >= buf->size_in_periods) {
				buf->w_in_periods -= buf->size_in_periods;
			}
#else
			if ((actual = snd_pcm_readi(driver->capture_handle, &buf->samples[buf->w_in_periods*2], avail)) < 0) {
					printf ("read failed %s\n", snd_strerror (actual));
					exit (1);
			}

			if((buf->w_in_periods += actual) >= buf->size_in_periods) {
				buf->w_in_periods -= buf->size_in_periods;
			}
#endif
			ret = avail;

		} else {
				//printf ("nothing to read\n");
		}
	}

	return ret;
}

int main(int argc, char *argv[])
{
	alsa_driver_t* driver;

	int r, w;
	int avail;
	buffer_t buf;

	printf("ALSA Path-through starting\n");

	buf.size_in_periods = BUFFER_SIZE_IN_PERIODS*2;
	buf.r_in_periods = 0;
	buf.w_in_periods = 0;
#if MMAP_ACCESS_ENABLED
	if(NULL == (buf.samples[0] = malloc(buf.size_in_periods*sizeof(int))))
	{
		printf("can not allocate memory for ring buffer\n");
		return 0;
	}
	if(NULL == (buf.samples[1] = malloc(buf.size_in_periods*sizeof(int))))
	{
		printf("can not allocate memory for ring buffer\n");
		return 0;
	}
#else
	if(NULL == (buf.samples = malloc(buf.size_in_periods*2*sizeof(int))))
	{
		printf("can not allocate memory for ring buffer\n");
		return 0;
	}
#endif

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

	r = 0;
	w = 0;

#if MMAP_ACCESS_ENABLED == 0
	printf("Audio Interface prepared for start\n");

	w = alsa_driver_write(driver, ProcessStereo, &buf);
	//printf("W: buf.w %d, buf.r %d, buf.size %d\n", buf.w_in_periods, buf.r_in_periods, buf.size_in_periods);
#endif

	printf("Audio started\n");

	while (1)
	{
#if POLLING_ENABLED
		int poll_time = alsa_driver_wait(driver);
		printf("poll %d, DSP usage %d%%\n",poll_time, (int)(100*poll_time/(float)PERIOD_TIME));
#endif

#if CAPTURE_ENABLED
		avail = alsa_driver_read(driver, &buf);
		//if(avail) printf("R: buf.w %d, buf.r %d, buf.size %d\n", buf.w_in_periods, buf.r_in_periods, buf.size_in_periods);
		r += avail;
		if(r >= SAMPLERATE)
		{
			printf(".");
			fflush(stdout);
			r = 0;
		}
#endif

		avail = alsa_driver_write(driver, ProcessStereo, &buf);
		//if(avail) printf("W: buf.w %d, buf.r %d, buf.size %d\n", buf.w_in_periods, buf.r_in_periods, buf.size_in_periods);

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
