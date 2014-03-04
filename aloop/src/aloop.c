/*
 * Copyright (c) 2012 Daniel Mack
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * See README
 *
 * SND_PCM_STREAM_PLAYBACK hw:0,0
 * SND_PCM_STREAM_CAPTUREK hw:0,0
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
//#include </usr/include/alsa/asoundlib.h>
#include <alsa/asoundlib.h>

#define SAMPLERATE	48000
#define BUFFER_TIME	100		// ms
#define PERIOD_TIME	10		// ms

//#define BUFSIZE		(SAMPLERATE * BUFFER_TIME / 1000) 	// sample rate x time in seconds

//#define BUFSIZE (1024 * 4)
#define BUFSIZE 1024*4

int buf[BUFSIZE * 2];

static unsigned int rate = SAMPLERATE;
static unsigned int format = SND_PCM_FORMAT_S32_LE;
static unsigned int buffer_time = BUFFER_TIME*1000; // time in usec
static unsigned int period_time = PERIOD_TIME*1000; // time in usec

snd_pcm_t *playback_handle, *capture_handle;

static void ProcessStereo(int* samples, int N)
{
	int i;
	int L, R;
	for(i = 0; i < N; ++i)
	{
		L = samples[i*2];
		R = samples[i*2+1];

		R = 0;

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
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
			name, dirname, snd_strerror(err));
		return err;
	}
	printf("Actual buffer time %d\n", buffer_time/1000);

	if ((err = snd_pcm_hw_params_set_period_time_near(*handle, hw_params, &period_time, NULL)) < 0) {
		fprintf(stderr, "%s (%s): cannot set sample rate(%s)\n",
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
	if ((err = snd_pcm_sw_params_set_avail_min(*handle, sw_params, BUFSIZE)) < 0) {
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

	return 0;
}

int main(int argc, char *argv[])
{
	int err;
	int i,j;

	printf("ALSA path through\n");

	if ((err = open_stream(&playback_handle, "hw:0,0", SND_PCM_STREAM_PLAYBACK)) < 0)
		return err;
	printf("Playback opened\n");

	if ((err = open_stream(&capture_handle, "hw:0,0", SND_PCM_STREAM_CAPTURE)) < 0)
		return err;
	printf("Capture opened\n");

	if ((err = snd_pcm_prepare(playback_handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use(%s)\n",
			 snd_strerror(err));
		return err;
	}
	printf("Audio Interface prepared\n");

	if ((err = snd_pcm_start(capture_handle)) < 0) {
		fprintf(stderr, "cannot prepare audio interface for use(%s)\n",
			 snd_strerror(err));
		return err;
	}
	printf("Audio started\n");

	memset(buf, 0, sizeof(buf));

	i = 0;
	j = 0;

	while (1)
	{
		int avail;
/*
		if ((err = snd_pcm_wait(playback_handle, 1000)) < 0) {
			fprintf(stderr, "poll failed(%s)\n", strerror(errno));
			break;
		}
*/

		avail = snd_pcm_avail_update(capture_handle);
		if (avail > 0)
		{
			if (avail > BUFSIZE)
				avail = BUFSIZE;

			snd_pcm_readi(capture_handle, buf, avail);

			ProcessStereo(buf,avail);

			//printf("r %d, ",avail);
			i += avail;
			if(i >= SAMPLERATE)
			{
				printf(".");
				i = 0;
			}
		}

		avail = snd_pcm_avail_update(playback_handle);
		if (avail > 0) {
			if (avail > BUFSIZE)
				avail = BUFSIZE;


			snd_pcm_writei(playback_handle, buf, avail);

			j += avail;
			if(j >= SAMPLERATE)
			{
				printf("+");
				j = 0;
			}
		}

		fflush(stdout);
	}

	snd_pcm_close(playback_handle);
	snd_pcm_close(capture_handle);
	return 0;
}
