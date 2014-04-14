/*
    Copyright (C) 2001 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __jack_driver_h__
#define __jack_driver_h__

#include <pthread.h>
#include <jack/types.h>

typedef float         gain_t;
typedef unsigned long channel_t;

typedef	enum  {
	Lock = 0x1,
	NoLock = 0x2,
	Sync = 0x4,
	NoSync = 0x8
} ClockSyncStatus;

typedef void (*ClockSyncListenerFunction)(channel_t,ClockSyncStatus,void*);

typedef struct {
    unsigned long id;
    ClockSyncListenerFunction function;
    void *arg;
} ClockSyncListener;

struct _jack_engine;
struct _jack_driver;
struct _jack_driver_nt;
struct jack_driver_t;

typedef int       (*JackDriverNTAttachFunction)(struct _jack_driver_nt *);
typedef int       (*JackDriverNTDetachFunction)(struct _jack_driver_nt *);
typedef int       (*JackDriverNTStopFunction)(struct _jack_driver_nt *);
typedef int       (*JackDriverNTStartFunction)(struct _jack_driver_nt *);
typedef int	  (*JackDriverNTBufSizeFunction)(struct _jack_driver_nt *,
					       jack_nframes_t nframes);
typedef int       (*JackDriverNTRunCycleFunction)(struct _jack_driver_nt *);

typedef struct _jack_driver_nt {

#define JACK_DRIVER_NT_DECL \
    jack_time_t period_usecs; \
    jack_time_t last_wait_ust; \
    jack_time_t (*get_microseconds)(void); \
    volatile int nt_run; \
    pthread_t nt_thread; \
    pthread_mutex_t nt_run_lock; \
    JackDriverNTAttachFunction nt_attach; \
    JackDriverNTDetachFunction nt_detach; \
    JackDriverNTStopFunction nt_stop; \
    JackDriverNTStartFunction nt_start; \
    JackDriverNTBufSizeFunction nt_bufsize; \
    JackDriverNTRunCycleFunction nt_run_cycle;
#define nt_read read
#define nt_write write
#define nt_null_cycle null_cycle

    JACK_DRIVER_NT_DECL


} jack_driver_nt_t;

#endif /* __jack_driver_h__ */
