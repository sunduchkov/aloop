/*
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.h
 *
 */

#include <math.h>
#include <getopt.h>
#include <inttypes.h>
#include "alsa_driver.h"

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

// default parameters (can be overwritten by command line options)

#define DEFAULT_SAMPLERATE		48000
#define DEFAULT_PERIOD_SIZE		96						// frames number between PCM interrupts (2 ms)
#define DEFAULT_BUFFER_SIZE		(2*DEFAULT_PERIOD_SIZE)	// ring buffer in frames. should be at least 2 periods
#define DEFAULT_PLAYBACK_DEVICE	"plughw:1,0"
#define DEFAULT_CAPTURE_DEVICE	"plughw:1,0"
#define DEFAULT_POLLING_USAGE	1

uint64_t alsa_get_microseconds()
{
	uint64_t t;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	t = (uint64_t) time.tv_sec * 1000000 + (uint64_t) time.tv_nsec / 1000;
	return t;
}

static int open_stream(alsa_driver_t* driver, snd_pcm_t **handle, const char *name, int dir)
{
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	const char *dirname = (dir == SND_PCM_STREAM_PLAYBACK) ? "PLAYBACK" : "CAPTURE";
	unsigned int format = SND_PCM_FORMAT_S32_LE;
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

	if ((err = snd_pcm_hw_params_set_buffer_size_near(*handle, hw_params, &driver->buffer_size)) < 0) {
		fprintf(stderr, "%s (%s): cannot set buffer time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual buffer size %d = %d [ms]\n", (int)driver->buffer_size, (int)driver->buffer_size*1000/driver->sample_rate);

	if ((err = snd_pcm_hw_params_set_period_size_near(*handle, hw_params, &driver->period_size, 0)) < 0) {
		fprintf(stderr, "%s (%s): cannot set period time (%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual period size %d = %d [ms]\n", (int)driver->period_size, (int)driver->period_size*1000/driver->sample_rate);

	if ((err = snd_pcm_hw_params_set_rate_near(*handle, hw_params, &driver->sample_rate, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual sample rate %d\n", driver->sample_rate);

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
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, driver->period_size)) < 0) {
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

/**
 *  print short information on the audio device
 */
static void
printCardInfo(snd_ctl_card_info_t*	ci)
{
	printf("Card info\n");
	printf("\tID         = %s\n", snd_ctl_card_info_get_id(ci));
	printf("\tDriver     = %s\n", snd_ctl_card_info_get_driver(ci));
	printf("\tName       = %s\n", snd_ctl_card_info_get_name(ci));
	printf("\tLongName   = %s\n", snd_ctl_card_info_get_longname(ci));
	printf("\tMixerName  = %s\n", snd_ctl_card_info_get_mixername(ci));
	printf("\tComponents = %s\n", snd_ctl_card_info_get_components(ci));
	printf("--------------\n");
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

int alsa_driver_new(alsa_driver_t* driver)
{
	snd_ctl_card_info_t *card_info;
	char * ctl_name;
	int err;
	uint64_t period_usecs;

	driver->playback_handle = NULL;
	driver->capture_handle = NULL;

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

	printCardInfo(card_info);

	driver->alsa_driver_name = strdup(snd_ctl_card_info_get_driver (card_info));

	free(ctl_name);

#if MMAP_ACCESS_ENABLED == 0
	if(NULL == (driver->samples = malloc(driver->buffer_size*NCHANNELS*sizeof(int)))) {
		printf("cannot allocate memory for sample buffer");
		return 0;
	}
#endif

	if ((err = open_stream(driver, &driver->playback_handle, driver->alsa_name_playback, SND_PCM_STREAM_PLAYBACK)) < 0)
		return 0;
	printf("Playback opened\n");

#if CAPTURE_ENABLED
	if ((err = open_stream(driver, &driver->capture_handle, driver->alsa_name_capture, SND_PCM_STREAM_CAPTURE)) < 0)
		return 0;
	printf("Capture opened\n");
#endif

	driver->capture_and_playback_not_synced = 0;

	if (driver->capture_handle && driver->playback_handle) {
		if (snd_pcm_link (driver->playback_handle,
				  driver->capture_handle) != 0) {
			driver->capture_and_playback_not_synced = 1;
		}
	}

	period_usecs = (uint64_t) floor ((((float) driver->period_size) / driver->sample_rate) * 1000000.0f);
	driver->polling_timeout = (int) floor (1.5f * period_usecs /1000.0);

	if(driver->use_polling) {
		printf("polling timeout %d [ms]\n", driver->polling_timeout);
	}

	driver->latency = driver->buffer_size * 1000 / driver->sample_rate;

	if(0 == driver->capture_and_playback_not_synced)
	{
		printf("Playback and Capture are synced\n");
	}

	return 1;
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

	driver->pfd = (struct pollfd *)
		malloc (sizeof (struct pollfd) *
			(driver->playback_nfds + driver->capture_nfds + 2));
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

	if (avail != driver->buffer_size) {
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
	memset (driver->samples, 0, driver->buffer_size*NCHANNELS*sizeof(int));

	if ((err = snd_pcm_writei(driver->playback_handle, driver->samples, avail)) < 0) {
		printf ("write failed %s\n", snd_strerror (err));
		exit (1);
	}
#endif
	return 1;
}

int alsa_driver_wait(alsa_driver_t* driver)
{
	if(!driver->use_polling) return 0;

	int ret;

#if USING_SYSTEM_POLL
	int i;

	int nfds = 0;
	int ci = 0;
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

	poll_start = alsa_get_microseconds();

	int poll_result = poll (driver->pfd, nfds, driver->polling_timeout);
	if (poll_result < 0) {

		if (errno == EINTR) {
			printf ("poll interrupt");
			return 0;
		}

		printf ("ALSA: poll call failed (%s)", strerror (errno));
		return 0;
	}

	poll_end = alsa_get_microseconds();
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
	if ((ret = snd_pcm_wait (driver->playback_handle, driver->polling_timeout)) == 0) {
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
		if (avail >= driver->avail_min)
		{
			if (avail > driver->buffer_size)
				avail = driver->buffer_size;

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
		if (avail >= driver->avail_min)
		{
			if (avail > driver->buffer_size)
				avail = driver->buffer_size;

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

int alsa_driver_get_options(alsa_driver_t* driver, int argc, char *argv[])
{
	int needhelp = 0;
	struct option long_option[] =
	{
		{"help", no_argument, NULL, 'h'},
		{"pdevice", required_argument, NULL, 'P'},
		{"cdevice", required_argument, NULL, 'C'},
		{"rate", required_argument, NULL, 'r'},
		{"period", required_argument, NULL, 'p'},
		{"buffer", required_argument, NULL, 'b'},
		{"wait", required_argument, NULL, 'w'},
		{NULL, 0, NULL, 0},
	};

	int err;
	int c;

	driver->sample_rate = DEFAULT_SAMPLERATE;
	driver->use_polling = DEFAULT_POLLING_USAGE;
	driver->period_size = DEFAULT_PERIOD_SIZE;
	driver->avail_min = DEFAULT_PERIOD_SIZE/2;
	driver->buffer_size = DEFAULT_BUFFER_SIZE;
	driver->alsa_name_playback = strdup(DEFAULT_PLAYBACK_DEVICE);
	driver->alsa_name_capture = strdup(DEFAULT_CAPTURE_DEVICE);

	while ((c = getopt_long(argc, argv, "hP:C:r:p:b:w:", long_option, NULL)) != -1) {
		switch (c) {
		case 'h':
			needhelp = 1;
			break;
		case 'P':
			driver->alsa_name_playback = strdup(optarg);
			break;
		case 'C':
			driver->alsa_name_capture = strdup(optarg);
			break;
		case 'r':
			err = atoi(optarg);
			driver->sample_rate = err >= 8000 && err <= 48000 ? err : DEFAULT_SAMPLERATE;
			break;
		case 'p':
			err = atoi(optarg);
			driver->period_size = err >= 32 && err < 200000 ? err : 0;
			driver->avail_min = driver->period_size/2;
			break;
		case 'b':
			err = atoi(optarg);
			driver->buffer_size = err >= 32 && err < 200000 ? err : 0;
			break;
		case 'w':
			err = atoi(optarg);
			driver->use_polling = err > 0 ? 1 : 0;
			break;
		}
	}

	if (needhelp) {
		printf(
				"Usage: aloop [OPTIONS]\n"
				"-h,--help      this message\n"
				"-P,--pdevice   playback device (plughw:1,0 by default - good for USB connected devices, try hw:0,0 for others)\n"
				"-C,--cdevice   capture device (plughw:1,0 by default)\n"
				"-r,--rate      sample rate in [Hz]\n"
				"-p,--period    period size in frames\n"
				"-b,--buffer    buffer size in frames (try 2 x period size first)\n"
				"-w,--wait      1 - wait for event, 0 - do not wait. Wait gives time to another threads - reduces overall CPU usage\n"
		);
	    printf("\n\n");
		return 0;
	}

	return 1;
}

