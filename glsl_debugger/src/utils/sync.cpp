/******************************************************************************

Copyright (C) 2006-2009 Institute for Visualization and Interactive Systems
(VIS), Universität Stuttgart.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice, this
	list of conditions and the following disclaimer in the documentation and/or
	other materials provided with the distribution.

  * Neither the name of the name of VIS, Universität Stuttgart nor the names
	of its contributors may be used to endorse or promote products derived from
	this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <time.h>

	#ifdef __MACH__
		#include <mach/clock.h>
		#include <mach/mach.h>
	#endif

#include <glsldebug_utils/sync.h>


/*
 * ::createIpcEvent
 */
int createIpcEvent(IpcEvent *evt, const int reserved, const int isInitiallySet,
        const int isCreateOnly, const char *name) {
    int retval = 0;

    char *n = (char *) malloc(strlen(name) + 2);

    if (n != NULL) {
        strcpy(n, "/");
        strcat(n, name);

        if ((*evt = sem_open(n, (isCreateOnly ? O_CREAT : 0), 0666, 
                (isInitiallySet ? 1 : 0))) == SEM_FAILED) {
            retval = errno;
        }

        free(n);
    } else {
        retval = ENOMEM;
    }

    return retval;
}


/*
 * ::setIpcEvent
 */
int setIpcEvent(IpcEvent evt) {

    /* Ensure maximum number of 1 in semaphore. */
    if ((sem_trywait(evt) == -1) && (errno != EAGAIN)) {
        return errno;
    }

    if (sem_post(evt) == -1) {
        return errno;
    }

}


/*
 * ::waitIpcEvent
 */
int waitIpcEvent(IpcEvent evt, int timeout) {
    struct timespec tsEnd;
    int retval = 0;

    if (timeout == TIMEOUT_INFINITE) {
        if (sem_wait(evt) == -1) {
            retval = errno;
        }

    } else {
    
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
		clock_serv_t cclock;
		mach_timespec_t mts;
		host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
		clock_get_time(cclock, &mts);
		mach_port_deallocate(mach_task_self(), cclock);
		tsEnd.tv_sec = mts.tv_sec;
		tsEnd.tv_nsec = mts.tv_nsec;
#else /* __MACH__ */
        clock_gettime(CLOCK_REALTIME, &tsEnd);
#endif
        tsEnd.tv_sec += timeout / 1000;
        tsEnd.tv_nsec += (timeout % 1000) * 1000;

        if (sem_timedwait(evt, &tsEnd) == -1) {
            retval = errno;
        }
    }

    return retval;
}


/*
 * ::deleteIpcEvent
 */
int deleteIpcEvent(IpcEvent evt) {
    /* TODO: Should unlink semaphore. Needs name. */
    /*
     * mueller: I assume that sem_close releases the memory, but I could not 
     * find any documentation about that.
     */
    return ((sem_close(evt) == -1) ? errno : 0);
}
