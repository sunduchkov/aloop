/*
 *	Audio path-through (tested on Beagleboard_xM, Ubuntu 13.10)
 *
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.c
 */

#include <stdio.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>

#define SAMPLERATE	48000
#define PERIOD_TIME	50                                  // PCM interrupt period [ms]
#define BUFFER_TIME	(2*PERIOD_TIME)                     // Buffer time [ms], should be at least 2xPERIOD_TIME
#define BUFFER_SIZE	(SAMPLERATE * BUFFER_TIME / 1000)   // sample rate [1/sec] x time [sec]
#define	LATENCY		(1000 * BUFFER_SIZE / SAMPLERATE)   // same as BUFFER_TIME [ms]

typedef struct _alsa_driver
{
    snd_pcm_t                    *playback_handle;
    snd_pcm_t                    *capture_handle;

    struct pollfd                *pfd;
    unsigned int                  playback_nfds;
    unsigned int                  capture_nfds;

    //snd_pcm_hw_params_t          *playback_hw_params;
    //snd_pcm_sw_params_t          *playback_sw_params;
    //snd_pcm_hw_params_t          *capture_hw_params;
    //snd_pcm_sw_params_t          *capture_sw_params;

    int                           capture_and_playback_not_synced;
	uint64_t                      poll_timeout;

    char                         *alsa_name_playback;
    char                         *alsa_name_capture;

} alsa_driver_t;

static void ProcessStereo(int* samples, int N)
{
	int i;
	int L, R;
	//static int k = 0;
	for(i = 0; i < N; ++i)
	{
		L = samples[i*2];
		R = samples[i*2+1];

		// Spectrum inversion for Right channel
		//if(i & 1) R = -R;

		// Square signal to Left channel
		//if(i & 0x40) L = (1<<30); else L = -(1<<30);

		samples[i*2] = L;
		samples[i*2+1] = R;
	}
}

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

	if ((err = snd_pcm_hw_params_set_access(*handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf(stderr, "%s (%s): cannot set access type(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_hw_params_set_format(*handle, hw_params, format)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample format(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}

	//
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
	//

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
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, BUFFER_SIZE)) < 0) {
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

alsa_driver_t* alsa_driver_new()
{
	alsa_driver_t* driver;
	int err;

	driver = (alsa_driver_t *) calloc (1, sizeof (alsa_driver_t));

	if ((err = open_stream(&driver->playback_handle, "hw:0,0", SND_PCM_STREAM_PLAYBACK)) < 0)
		return NULL;
	printf("Playback opened\n");

	if ((err = open_stream(&driver->capture_handle, "hw:0,0", SND_PCM_STREAM_CAPTURE)) < 0)
		return NULL;
	printf("Capture opened\n");

	driver->capture_and_playback_not_synced = 0;

	if (driver->capture_handle && driver->playback_handle) {
		if (snd_pcm_link (driver->playback_handle,
				  driver->capture_handle) != 0) {
			driver->capture_and_playback_not_synced = 1;
		}
	}

	if(0 == driver->capture_and_playback_not_synced)
	{
		printf("Playback and Capture are synced\n");
	}

	return driver;
}

int alsa_driver_start(alsa_driver_t* driver)
{
	int err;

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

#if 0 // is not needed ??
	if ((err = snd_pcm_start (driver->playback_handle)) < 0) {
		fprintf(stderr, "cannot start audio interface for playback (%s)\n",
			 snd_strerror(err));
		return 0;
	}

	if ((driver->capture_handle && driver->capture_and_playback_not_synced)
	    || !driver->playback_handle) {
		if ((err = snd_pcm_start (driver->capture_handle)) < 0) {
			printf ("ALSA: could not start capture (%s)",
				    snd_strerror (err));
			return 0;
		}
	}
#endif
	return 1;
}

int alsa_driver_wait(alsa_driver_t* driver)
{
	int i;

	int nfds = 0;

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
		//ci = nfds;
		nfds += driver->capture_nfds;
	}

	/* ALSA doesn't set POLLERR in some versions of 0.9.X */

	for (i = 0; i < nfds; i++) {
		driver->pfd[i].events |= POLLERR;
	}

#if 0
	if (extra_fd >= 0) {
		driver->pfd[nfds].fd = extra_fd;
		driver->pfd[nfds].events =
			POLLIN|POLLERR|POLLHUP|POLLNVAL;
		nfds++;
	}
#endif

//		poll_enter = driver->get_microseconds ();

#if 0
	if (poll_enter > driver->poll_next) {
		/*
		 * This processing cycle was delayed past the
		 * next due interrupt!  Do not account this as
		 * a wakeup delay:
		 */
		driver->poll_next = 0;
		driver->poll_late++;
	}
#endif

	int poll_result = poll (driver->pfd, nfds, driver->poll_timeout);
	if (poll_result < 0) {

		if (errno == EINTR) {
			printf ("poll interrupt");
			return 0;
		}

		printf ("ALSA: poll call failed (%s)", strerror (errno));
		return 0;
	}

//		poll_ret = driver->get_microseconds ();
	return 1;
}

int main(int argc, char *argv[])
{
	alsa_driver_t* driver;

	//snd_pcm_t *playback_handle, *capture_handle;
	int buf[BUFFER_SIZE*2]; // 2 - for stereo
	int i,j;
	memset(buf,0,sizeof(buf));

	printf("ALSA Path-through starting\n");

	if(NULL == (driver = alsa_driver_new()))
	{
		printf("alsa_driver_new not succeeded\n");
		return 0;
	}

	printf("Audio Interface prepared with %d [ms] latency\n", LATENCY);

	if(!alsa_driver_start(driver))
	{
		printf("alsa_driver_start not succeeded\n");
		return 0;
	}

	printf("Audio started\n");

	i = 0;
	j = 0;

	while (1)
	{
		int avail;

		alsa_driver_wait(driver);

		if(driver->capture_handle)
		{
			avail = snd_pcm_avail_update(driver->capture_handle);
			if (avail >= 1024)
			{
				if (avail > BUFFER_SIZE)
					avail = BUFFER_SIZE;

				snd_pcm_readi(driver->capture_handle, buf, avail);

				ProcessStereo(buf,avail);

				//printf("r %d, ",avail);
				i += avail;
				if(i >= SAMPLERATE)
				{
					printf(".");
					fflush(stdout);
					i = 0;
				}
			}
		}

		if(driver->playback_handle)
		{
			avail = snd_pcm_avail_update(driver->playback_handle);
			if (avail > 1024)
			{
				if (avail > BUFFER_SIZE)
					avail = BUFFER_SIZE;


				snd_pcm_writei(driver->playback_handle, buf, avail);

				//printf("w %d, ",avail);
				j += avail;
				if(j >= SAMPLERATE)
				{
					printf("+");
					fflush(stdout);
					j = 0;
				}
			}
		}

	}

	return 0;
}
