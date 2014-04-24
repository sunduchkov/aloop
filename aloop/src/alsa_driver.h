/*
 *	ALSA driver based on Paul Davis driver
 *
 *	https://github.com/jackaudio/jack1/blob/master/drivers/alsa/alsa_driver.c
 *
 */

#ifndef __ALSA_DRIVER_H__
#define __ALSA_DRIVER_H__

#include <alsa/asoundlib.h>
#include <alsa/pcm.h>

// fixed parameters

#define NCHANNELS				2				// stereo signal, two samples in one frame
#define CAPTURE_ENABLED			1
#define MMAP_ACCESS_ENABLED		0
#define	USING_SYSTEM_POLL		1

typedef enum {
	ALSA_STATUS_OK = 0,
	ALSA_STATUS_NULL_POINTER,
	ALSA_STATUS_MEMORY_ERROR,
	ALSA_STATUS_ERROR,

} 	alsa_status_t;

typedef struct _alsa_driver
{
    snd_pcm_t              	*playback_handle;
    snd_pcm_t               *capture_handle;
    snd_ctl_t               *ctl_handle;

    char                    *alsa_driver_name;
    char                    *alsa_name_playback;
    char                    *alsa_name_capture;

#if MMAP_ACCESS_ENABLED
    int 					*capture_addr[NCHANNELS];
    int 					*playback_addr[NCHANNELS];
    unsigned long           capture_interleave_skip[NCHANNELS];
    unsigned long           playback_interleave_skip[NCHANNELS];
#else
    int 					*samples;
#endif

#if USING_SYSTEM_POLL
    struct pollfd           *pfd;
    unsigned int            playback_nfds;
    unsigned int            capture_nfds;
#endif

    int                     capture_and_playback_not_synced;

	unsigned int			sample_rate;
	snd_pcm_uframes_t		period_size;
	snd_pcm_uframes_t		buffer_size;
	snd_pcm_uframes_t		avail_min;
	int						latency;
	int						use_polling;
	int                    	polling_timeout;

	int 					(*puts)(const char* s);

}	alsa_driver_t;

uint64_t alsa_get_microseconds();

alsa_status_t alsa_driver_get_options(alsa_driver_t* driver, int argc, char *argv[]);

alsa_status_t alsa_driver_open(alsa_driver_t* driver);
alsa_status_t alsa_driver_close(alsa_driver_t* driver);

alsa_status_t alsa_driver_prepare(alsa_driver_t* driver);
alsa_status_t alsa_driver_start(alsa_driver_t* driver);

alsa_status_t alsa_driver_wait(alsa_driver_t* driver, int* polling_time);
alsa_status_t alsa_driver_read(alsa_driver_t* driver);
alsa_status_t alsa_driver_write_prepare(alsa_driver_t* driver, int** addr, int* size);
alsa_status_t alsa_driver_write(alsa_driver_t* driver);

#endif // __ALSA_DRIVER_H__
