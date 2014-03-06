/*
 *	ALSA Path-through (tested on Pandaboard, Ubuntu 13.10)
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#define SAMPLERATE	48000
#define PERIOD_TIME	10					// PCM interrupt period [ms]
#define BUFFER_TIME	(2*PERIOD_TIME)		// Buffer time [ms], should be at least 2xPERIOD_TIME

#define BUFFER_SIZE	(SAMPLERATE * BUFFER_TIME / 1000) 	// sample rate [1/sec] x time [sec]

static void ProcessStereo(int* samples, int N)
{
	int i;
	int L, R;
	for(i = 0; i < N; ++i)
	{
		L = samples[i*2];
		R = samples[i*2+1];

		// Spectrum inversion for Right channel
		if(i & 1) R = -R;

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

	return 0;
}

int main(int argc, char *argv[])
{
	snd_pcm_t *playback_handle, *capture_handle;
	int buf[BUFFER_SIZE*2]; // 2 - for stereo
	int err;
	int i,j;

	printf("ALSA Path-through starting\n");

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
			if (avail > BUFFER_SIZE)
				avail = BUFFER_SIZE;

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
			if (avail > BUFFER_SIZE)
				avail = BUFFER_SIZE;


			snd_pcm_writei(playback_handle, buf, avail);

			//printf("w %d, ",avail);
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
