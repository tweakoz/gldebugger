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

#include <assert.h>

#include <QtGui/QApplication>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/shm.h>
#include <sched.h>

#include <errno.h>
#include <glsldebug_utils/dbgprint.h>

#include <ork/fixedstring.h>
#include <ork/path.h>
#include <ork/ipcq.h>
#include <ork/thread.h>

#ifdef GLSLDB_OSX
#  include <signal.h>
#  include "utils/osx_ptrace_defs.h"
#  define __ptrace_request int
#endif

#ifndef PTRACE_SETOPTIONS
/* from linux/ptrace.h */
#  define PTRACE_SETOPTIONS   0x4200
#  define PTRACE_GETEVENTMSG  0x4201
#  define PTRACE_GETSIGINFO   0x4202
#  define PTRACE_SETSIGINFO   0x4203

/* options set using PTRACE_SETOPTIONS */
#  define PTRACE_O_TRACESYSGOOD   0x00000001
#  define PTRACE_O_TRACEFORK  0x00000002
#  define PTRACE_O_TRACEVFORK 0x00000004
#  define PTRACE_O_TRACECLONE 0x00000008
#  define PTRACE_O_TRACEEXEC  0x00000010
#  define PTRACE_O_TRACEVFORKDONE 0x00000020
#  define PTRACE_O_TRACEEXIT  0x00000040

#  define PTRACE_O_MASK       0x0000007f

/* Wait extended result codes for the above trace options.  */
#  define PTRACE_EVENT_FORK   1
#  define PTRACE_EVENT_VFORK  2
#  define PTRACE_EVENT_CLONE  3
#  define PTRACE_EVENT_EXEC   4
#  define PTRACE_EVENT_VFORK_DONE 5
#  define PTRACE_EVENT_EXIT   6

#endif

#include "progControl.qt.h"

/////////////////////////////////////////////////////////////////////////////////

#define DEBUGLIB "/../lib/libglsl.ipose.ix.release.so"
#define LIBDLSYM "/../lib/libglsl.enums.ix.release.so"
#define DBG_FUNCTIONS_PATH "/../lib/plugins"

/////////////////////////////////////////////////////////////////////////////////

ProgramControl::ProgramControl(const char *pname)
	: debuggedProgramPID(0)
	, mSender(nullptr)
	, mReciever(nullptr)
{
    buildEnvVars(pname);
    initShmem();
}

////////////////////////////////////////////////////////////////////////

ProgramControl::~ProgramControl()
{
    dbgPrint(DBGLVL_DEBUG, "~ProgramControl freeShmem()\n");
    freeShmem();
}

////////////////////////////////////////////////////////////////////////

bool ProgramControl::childAlive(void)
{
	int status = 15;
	pid_t pid = -1;
	
	dbgPrint(DBGLVL_INFO, "get childStatus...\n");
	pid = waitpid(debuggedProgramPID, &status, WUNTRACED |WNOHANG);
		
	if (pid == -1) {
		dbgPrint(DBGLVL_WARNING, "no child!\n");
		return false;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::checkChildStatus(void)
{

	int status = 15;
	pid_t pid = -1;
	int errorStatus = EINTR;
	ALIGNED_DATA newPid;
	
	while (pid == -1 && errorStatus == EINTR) {
		printf("SUP>>checkChildStatus...\n");
		

		pid = waitpid(debuggedProgramPID, &status, WUNTRACED);
		
		printf("SUP>>CheckChildStatus waitpid<%d> status<0x%08x>\n", pid, status );;
		/* from gdb: Try again with __WCLONE to check cloned processes. */
		if (pid == -1 && errno == ECHILD) {
#ifndef GLSLDB_OSX
			dbgPrint(DBGLVL_INFO, "check clones: %i", (int)pid);
			pid = waitpid (debuggedProgramPID, &status, __WCLONE);
#else
			/* Ack, ugly ugly hack --
	  		   wait() doesn't work, waitpid() doesn't work, and ignoring SIG_CHLD
	   		   doesn't work .. and the child thread is still a zombie, so kill()
	   		   doesn't work.
			*/
			char command[1024];

			sprintf(command,
				"ps ax|fgrep -v fgrep|fgrep -v '<zombie>'|fgrep %d >/dev/null",
				debuggedProgramPID);
			while ( system(command) == 0 )
				sleep(1);
#endif
			dbgPrint(DBGLVL_INFO, " %i\n", pid);
			errorStatus = errno;
		}
		
		if (pid != -1 && WIFSTOPPED (status) && WSTOPSIG (status) == SIGSTOP &&
			pid != debuggedProgramPID) {
			dbgPrint(DBGLVL_WARNING, "New pid: %i\n", (int)pid);
			errorStatus = EINTR;
		}
	}

	if (pid == -1) {
		dbgPrint(DBGLVL_WARNING, "Error: no child!\n");
		return PCE_EXIT;
	}

	/* handle extended wait status for trace events */
	switch (status >> 16) {
		case PTRACE_EVENT_CLONE:
#if 0
			ptrace((__ptrace_request)PTRACE_GETEVENTMSG, pid, 0, &newPid);
			dbgPrint(DBGLVL_INFO, "extended wait status: PTRACE_EVENT_CLONE new pid: %i FIXME!!!!!!!!!!!\n", newPid);
#else
			dbgPrint(DBGLVL_WARNING, "extended wait status: PTRACE_EVENT_CLONE ... FIXME!!!!!!!!!!!\n");
#endif
			break;
		case PTRACE_EVENT_FORK:
		case PTRACE_EVENT_VFORK:
#ifndef GLSLDB_OSX
			ptrace((__ptrace_request)PTRACE_GETEVENTMSG, pid, 0, &newPid);
			dbgPrint(DBGLVL_INFO, "extended wait status: PTRACE_EVENT_FORK or "
			                "PTRACE_EVENT_VFORKi with pid %i\n", (int)newPid);
#endif
			break;
		case PTRACE_EVENT_EXEC:
			dbgPrint(DBGLVL_INFO, "extended wait status: PTRACE_EVENT_EXEC\n");
			break;
		default:
			break;
	}
	
    if (WIFEXITED(status)) {
        dbgPrint(DBGLVL_INFO, "child terminated normally with status %i\n",
                WEXITSTATUS(status));
		return PCE_EXIT;
    } else if (WIFSIGNALED(status)) {
        dbgPrint(DBGLVL_INFO, "child terminated by signal %i\n", WTERMSIG(status));
		switch (WTERMSIG(status)) {
			case SIGHUP:
			case SIGINT:
			case SIGQUIT:
			case SIGILL: 
			case SIGABRT:
			case SIGFPE: 
			case SIGKILL:
			case SIGSEGV:
			case SIGPIPE:
			case SIGALRM:
			case SIGTERM:
			case SIGUSR1:
			case SIGUSR2:
			case SIGBUS:
#ifndef GLSLDB_OSX
			case SIGPOLL:
#endif
			case SIGPROF:
			case SIGSYS:
			case SIGXFSZ:
			case SIGXCPU:
			case SIGVTALRM:
				return PCE_EXIT;
			default:
				dbgPrint(DBGLVL_WARNING, "Unhandled signal %i\n", WTERMSIG(status));
				return PCE_EXIT;
		}
		return PCE_NONE;
	} else if (WIFSTOPPED(status)) {
		switch (WSTOPSIG(status)) {
			case SIGSTOP:
			case SIGTRAP:
				dbgPrint(DBGLVL_DEBUG, "SUP>>child process was stopped by signal %i\n",
						WSTOPSIG(status));
				return PCE_NONE;
			case SIGHUP:
			case SIGINT:
			case SIGQUIT:
			case SIGILL: 
			case SIGABRT:
			case SIGFPE: 
			case SIGKILL:
			case SIGSEGV:
			case SIGPIPE:
			case SIGALRM:
			case SIGTERM:
			case SIGUSR1:
			case SIGUSR2:
			case SIGBUS:
#ifndef GLSLDB_OSX
			case SIGPOLL:
#endif
			case SIGPROF:
			case SIGSYS:
			case SIGXFSZ:
			case SIGXCPU:
			case SIGVTALRM:
			default:
				dbgPrint(DBGLVL_INFO, "SUP>>child process was stopped by signal %i\n",
						WSTOPSIG(status));
				return PCE_EXIT;
		}
#ifdef WIFCONTINUED
	} else if (WIFCONTINUED(status)) {
		dbgPrint(DBGLVL_INFO, "child process was resumed by delivery of SIGCONT\n");
		return PCE_NONE;
#endif
	} else {
        dbgPrint(DBGLVL_WARNING, "child terminated with unknown reason\n");
    	return PCE_EXIT;
    }

}

////////////////////////////////////////////////////////////////////////
// continue execution on the inferior
//  wait until it yields control back to the debugger
////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::executeDbgCommand(void)
{
	static int continue_count = 0;
    errno = 0;
	printf( "SUP>>continue_count<%d> begin\n", continue_count );
    ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
    assert(errno==0);
	pcErrorCode errc = checkChildStatus();
	printf( "SUP>>continue_count<%d> end\n", continue_count );

	continue_count++;
	return errc;

}
	
////////////////////////////////////////////////////////////////////////

void ProgramControl::setDebugEnvVars(void)
{
	char *s = NULL;

	//////////////////////////////////////////////////
	// LD_PRELOAD
	//////////////////////////////////////////////////
    printf( "SUP>>set LD_PRELOAD<%s>\n", debuglib.c_str() );
    if (setenv("LD_PRELOAD", debuglib.c_str(), 1)) {
        dbgPrint(DBGLVL_ERROR, "setenv LD_PRELOAD failed: %s\n", strerror(errno));
    }
	//////////////////////////////////////////////////
    dbgPrint(DBGLVL_INFO, "SUP>>env dbglib: \"%s\"\n", debuglib.c_str());
    asprintf(&s, "%i", shmid);
    if (setenv("GLSL_DEBUGGER_SHMID", s, 1)) {
        dbgPrint(DBGLVL_ERROR, "setenv GLSL_DEBUGGER_SHMID failed: %s\n", strerror(errno));
    }
    dbgPrint(DBGLVL_INFO, "SUP>>env shmid: \"%s\"\n", s);
    free(s);
    
    if (setenv("GLSL_DEBUGGER_DBGFCTNS_PATH", dbgFunctionsPath.c_str(), 1)) {
        dbgPrint(DBGLVL_ERROR, "setenv GLSL_DEBUGGER_DBGFCTNS_PATH failed: %s\n", strerror(errno));
    }
    dbgPrint(DBGLVL_INFO, "SUP>>env dbgfctns: \"%s\"\n", dbgFunctionsPath.c_str());
    
    if (setenv("GLSL_DEBUGGER_LIBDLSYM", libdlsym.c_str(), 1)) {
        dbgPrint(DBGLVL_ERROR, "setenv LIBDLSYM failed: %s\n", strerror(errno));
    }
    dbgPrint(DBGLVL_DEBUG, "SUP>>libdlsym: \"%s\"\n", libdlsym.c_str());
	
    asprintf(&s, "%i", getMaxDebugOutputLevel());
    if (setenv("GLSL_DEBUGGER_LOGLEVEL", s, 1)) {
        dbgPrint(DBGLVL_ERROR, "setenv GLSL_DEBUGGER_LOGLEVEL failed: %s\n", strerror(errno));
    }
    dbgPrint(DBGLVL_INFO, "SUP>>env dbglvl: \"%s\"\n", s);
	free(s);

	if (logdir.length())
	{
    	if (setenv("GLSL_DEBUGGER_LOGDIR", logdir.c_str(), 1)) {
        	dbgPrint(DBGLVL_ERROR, "setenv GLSL_DEBUGGER_LOGDIR failed: %s\n", strerror(errno));
    	}
    	dbgPrint(DBGLVL_INFO, "env dbglogdir: \"%s\"\n", logdir.c_str());
	}

}

////////////////////////////////////////////////////////////////////////

void ProgramControl::buildEnvVars(const char *pname)
{
    //////////////////////////////////
    // get current exe name
    //////////////////////////////////
	ork::Path::NameType cur_proc_exe,progPathL,progPathR;
	cur_proc_exe.format("/proc/%d/exe", getpid());
    ork::Path progPath = cur_proc_exe.c_str();
	progPath.FollowSymLink();
	progPath.SplitR(progPathL,progPathR,'/');
	progPath = progPathL;
    dbgPrint(DBGLVL_INFO, "SUP>>Absolute path to debugger executable is: %s\n", progPath.c_str());
    //////////////////////////////////
    // preload library 
    //////////////////////////////////
    debuglib = (progPath+DEBUGLIB).c_str();
    dbgPrint(DBGLVL_INFO, "SUP>>Path to debug library is: %s\n", debuglib.c_str());
    //////////////////////////////////
    // path to debug SOs 
    //////////////////////////////////
    dbgFunctionsPath = (progPath+DBG_FUNCTIONS_PATH).c_str();
    dbgPrint(DBGLVL_INFO, "SUP>>Path to debug functions is: %s\n", dbgFunctionsPath.c_str());
    //////////////////////////////////
    // dlsym helper library 
    //////////////////////////////////
    libdlsym = (progPath+LIBDLSYM).c_str();
    dbgPrint(DBGLVL_INFO, "SUP>>Path to libdlsym is: %s\n", libdlsym.c_str());
    //////////////////////////////////
    // log dir 
    //////////////////////////////////
	if (getLogDir())
	{	logdir = realpath(getLogDir(), NULL);
		dbgPrint(DBGLVL_INFO, "SUP>>LogDir is: %s\n", logdir.c_str());
	}
	else
		logdir = "";
    //////////////////////////////////
}

////////////////////////////////////////////////////////////////////////

DbgRec* ProgramControl::getThreadRecord(pid_t pid)
{
    int i;
    for (i = 0; i < SHM_MAX_THREADS; i++) {
        if (fcalls[i].threadId == 0 || (pid_t)fcalls[i].threadId == pid) {
            break;
        }
    }
    if (i == SHM_MAX_THREADS) {
        dbgPrint(DBGLVL_ERROR, "Error: max. number of debuggable threads exceeded!\n");
        exit(1);
    }
    return &fcalls[i];
}

////////////////////////////////////////////////////////////////////////

unsigned int ProgramControl::getArgumentSize(int type)
{
    switch (type) {
        case DBG_TYPE_CHAR:
            return sizeof(char);
        case DBG_TYPE_UNSIGNED_CHAR:
            return sizeof(unsigned char);
        case DBG_TYPE_SHORT_INT:
            return sizeof(short);
        case DBG_TYPE_UNSIGNED_SHORT_INT:
            return sizeof(unsigned short);
        case DBG_TYPE_INT:
            return sizeof(int);
        case DBG_TYPE_UNSIGNED_INT:
            return sizeof(unsigned int);
        case DBG_TYPE_LONG_INT:
            return sizeof(long);
        case DBG_TYPE_UNSIGNED_LONG_INT:
            return sizeof(unsigned long);
        case DBG_TYPE_LONG_LONG_INT:
            return sizeof(long long);
        case DBG_TYPE_UNSIGNED_LONG_LONG_INT:
            return sizeof(unsigned long long);
        case DBG_TYPE_FLOAT:
            return sizeof(float);
        case DBG_TYPE_DOUBLE:
            return sizeof(double);
        case DBG_TYPE_POINTER:
            return sizeof(void*);
        case DBG_TYPE_BOOLEAN:
            return sizeof(GLboolean);
        case DBG_TYPE_BITFIELD:
            return sizeof(GLbitfield);
        case DBG_TYPE_ENUM:
            return sizeof(GLbitfield);
		case DBG_TYPE_STRUCT:
			return 0; /* FIXME */
        default:
            dbgPrint(DBGLVL_WARNING, "invalid argument type\n");
            return 0;
    }
}

////////////////////////////////////////////////////////////////////////

void* ProgramControl::copyArgumentFromProcess(void *addr, int type)
{
    void *r = NULL;
    
    r = malloc(getArgumentSize(type));
    cpyFromProcess(debuggedProgramPID, r, addr, getArgumentSize(type));

    return r;
}

////////////////////////////////////////////////////////////////////////

void ProgramControl::copyArgumentToProcess(void *dst, void *src, int type)
{
    cpyToProcess(debuggedProgramPID, dst, src, getArgumentSize(type));
}

////////////////////////////////////////////////////////////////////////

char* ProgramControl::printArgument(void *addr, int type)
{
    char *argString;
	char *s;
	/* FIXME */
	int *tmp = (int*) malloc(sizeof(double)+sizeof(long long));

	switch (type) {
	case DBG_TYPE_CHAR:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(char));
		dbgPrintNoPrefix(DBGLVL_INFO, "%i", *(char*)tmp); 
		asprintf(&argString, "%i", *(char*)tmp); 
		break;
	case DBG_TYPE_UNSIGNED_CHAR:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(unsigned char));
		dbgPrintNoPrefix(DBGLVL_INFO, "%i", *(unsigned char*)tmp); 
		asprintf(&argString, "%i", *(unsigned char*)tmp); 
		break;
	case DBG_TYPE_SHORT_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(short));
		dbgPrintNoPrefix(DBGLVL_INFO, "%i", *(short*)tmp); 
		asprintf(&argString, "%i", *(short*)tmp); 
		break;
	case DBG_TYPE_UNSIGNED_SHORT_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(unsigned short));
		dbgPrintNoPrefix(DBGLVL_INFO, "%i", *(unsigned short*)tmp); 
		asprintf(&argString, "%i", *(unsigned short*)tmp); 
		break;
	case DBG_TYPE_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(int));
		dbgPrintNoPrefix(DBGLVL_INFO, "%i", *(int*)tmp); 
		asprintf(&argString, "%i", *(int*)tmp); 
		break;
	case DBG_TYPE_UNSIGNED_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(unsigned int));
		dbgPrintNoPrefix(DBGLVL_INFO, "%u", *(unsigned int*)tmp); 
		asprintf(&argString, "%u", *(unsigned int*)tmp); 
		break;
	case DBG_TYPE_LONG_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(long));
		dbgPrintNoPrefix(DBGLVL_INFO, "%li", *(long*)tmp); 
		asprintf(&argString, "%li", *(long*)tmp); 
		break;
	case DBG_TYPE_UNSIGNED_LONG_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(unsigned long));
		dbgPrintNoPrefix(DBGLVL_INFO, "%lu", *(unsigned long*)tmp); 
		asprintf(&argString, "%lu", *(unsigned long*)tmp); 
		break;
	case DBG_TYPE_LONG_LONG_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(long long));
		dbgPrintNoPrefix(DBGLVL_INFO, "%lli", *(long long*)tmp); 
		asprintf(&argString, "%lli", *(long long*)tmp); 
		break;
	case DBG_TYPE_UNSIGNED_LONG_LONG_INT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(unsigned long long));
		dbgPrintNoPrefix(DBGLVL_INFO, "%llu", *(unsigned long long*)tmp); 
		asprintf(&argString, "%llu", *(unsigned long long*)tmp); 
		break;
	case DBG_TYPE_FLOAT:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(float));
		dbgPrintNoPrefix(DBGLVL_INFO, "%f", *(float*)tmp); 
		asprintf(&argString, "%f", *(float*)tmp); 
		break;
	case DBG_TYPE_DOUBLE:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(double));
		dbgPrintNoPrefix(DBGLVL_INFO, "%f", *(double*)tmp); 
		asprintf(&argString, "%f", *(double*)tmp); 
		break;
	case DBG_TYPE_POINTER:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(void*));
		dbgPrintNoPrefix(DBGLVL_INFO, "%p", *(void**)tmp); 
		asprintf(&argString, "%p", *(void**)tmp); 
		break;
	case DBG_TYPE_BOOLEAN:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(GLboolean));
		dbgPrintNoPrefix(DBGLVL_INFO, "%s", *(GLboolean*)tmp ? "TRUE" : "FALSE");
		asprintf(&argString, "%s", *(GLboolean*)tmp ? "TRUE" : "FALSE");
		break;
	case DBG_TYPE_BITFIELD:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(GLbitfield));
		s  = dissectBitfield(*(GLbitfield*)tmp);
		dbgPrintNoPrefix(DBGLVL_INFO, "%s", s);
		asprintf(&argString, "%s", s);
		free(s);
		break;
	case DBG_TYPE_ENUM:
		cpyFromProcess(debuggedProgramPID, tmp, addr, sizeof(GLenum));
		dbgPrintNoPrefix(DBGLVL_INFO, "%s", lookupEnum(*(GLenum*)tmp));
		asprintf(&argString, "%s", lookupEnum(*(GLenum*)tmp));
		break;
	case DBG_TYPE_STRUCT:
		dbgPrintNoPrefix(DBGLVL_INFO, "STRUCT");
		asprintf(&argString, "STRUCT");
		break;
	default:	
		dbgPrintNoPrefix(DBGLVL_INFO, "UNKNOWN TYPE [%i]", type);
		asprintf(&argString, "UNKNOWN_TYPE[%i]", type);
	}
	free(tmp);
    return argString;
}

////////////////////////////////////////////////////////////////////////
/* TODO: obsolete, only for debugging */
////////////////////////////////////////////////////////////////////////

void ProgramControl::printCall()
{
    int i;
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    if (!rec) {
        dbgPrint(DBGLVL_ERROR, "no rec\n");
        exit(1);
    }
    
    dbgPrint(DBGLVL_INFO, "call: %s(", rec->fname);

    for (i = 0; i < (int)rec->numItems; i++) {
        dbgPrintNoPrefix(DBGLVL_INFO, "(%p,%li)", (void*)rec->items[2*i], (long)rec->items[2*i+1]);
        if (i != (int)rec->numItems - 1) {
            dbgPrintNoPrefix(DBGLVL_INFO, ", ");
        }
    }
    dbgPrintNoPrefix(DBGLVL_INFO, ")\n");
}

////////////////////////////////////////////////////////////////////////
/* TODO: obsolete, only for debugging */
////////////////////////////////////////////////////////////////////////

void ProgramControl::printResult()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	
	if (rec->result == DBG_ERROR_CODE) {
		/* function without return value */
	} else if (rec->result == DBG_RETURN_VALUE) {
        dbgPrint(DBGLVL_INFO, "SUP>>result: (%p,%li) ", 
                (void*)rec->items[0], (long)rec->items[1]);
        printArgument((void*)rec->items[0], rec->items[1]);
        dbgPrintNoPrefix(DBGLVL_INFO, "\n");
    } else {
		dbgPrint(DBGLVL_WARNING, "Hmm: Result expected but got code %i\n",
		        (unsigned int)rec->result);
	}
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::checkError()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	if (rec->result == DBG_ERROR_CODE) {
		switch ((unsigned int)rec->items[0]) {
			/* TODO: keep in sync with debuglib.h and errorCodes.h */
			case DBG_NO_ERROR:
				return PCE_NONE;
			/* debuglib errors */
			case DBG_ERROR_NO_ACTIVE_SHADER:
				return PCE_DBG_NO_ACTIVE_SHADER;
			case DBG_ERROR_NO_SUCH_DBG_FUNC:
				return PCE_DBG_NO_SUCH_DBG_FUNC;
			case DBG_ERROR_MEMORY_ALLOCATION_FAILED:
				return PCE_DBG_MEMORY_ALLOCATION_FAILED;
			case DBG_ERROR_DBG_SHADER_COMPILE_FAILED:
				return PCE_DBG_DBG_SHADER_COMPILE_FAILED;
			case DBG_ERROR_DBG_SHADER_LINK_FAILED:
				return PCE_DBG_DBG_SHADER_LINK_FAILED;
			case DBG_ERROR_NO_STORED_SHADER:
				return PCE_DBG_NO_STORED_SHADER;
			case DBG_ERROR_READBACK_INVALID_COMPONENTS:
				return PCE_DBG_READBACK_INVALID_COMPONENTS;
			case DBG_ERROR_READBACK_INVALID_FORMAT:
				return PCE_DBG_READBACK_INVALID_FORMAT;
			case DBG_ERROR_READBACK_NOT_ALLOWED:
				return PCE_DBG_READBACK_NOT_ALLOWED;
			case DBG_ERROR_OPERATION_NOT_ALLOWED:
				return PCE_DBG_OPERATION_NOT_ALLOWED;
			case DBG_ERROR_INVALID_OPERATION:
				return PCE_DBG_INVALID_OPERATION;
			case DBG_ERROR_INVALID_VALUE:
				return PCE_DBG_INVALID_VALUE;
			/* gl errors */
			case GL_INVALID_ENUM:
				return PCE_GL_INVALID_ENUM;
			case GL_INVALID_VALUE:
				return PCE_GL_INVALID_VALUE;
			case GL_INVALID_OPERATION:
				return PCE_GL_INVALID_OPERATION;
			case GL_STACK_OVERFLOW:
				return PCE_GL_STACK_OVERFLOW;
			case GL_STACK_UNDERFLOW:
				return PCE_GL_STACK_UNDERFLOW;
			case GL_OUT_OF_MEMORY:
				return PCE_GL_OUT_OF_MEMORY;
			case GL_TABLE_TOO_LARGE:
				return PCE_GL_TABLE_TOO_LARGE;
			case GL_INVALID_FRAMEBUFFER_OPERATION_EXT:
				return PCE_GL_INVALID_FRAMEBUFFER_OPERATION_EXT;
			default:
				dbgPrint(DBGLVL_WARNING, "checkError got error code %i\n", (unsigned int)rec->items[0]);
				return PCE_UNKNOWN_ERROR;
		}
	}
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandStopExecution(void)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_STOP_EXECUTION\n");
    rec->operation = DBG_STOP_EXECUTION;
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandExecute(bool stopOnGLError)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_EXECUTE (DBG_EXECUTE_RUN)\n");
    rec->operation = DBG_EXECUTE;
	rec->items[0] = DBG_EXECUTE_RUN;
	rec->items[1] = stopOnGLError ? 1 : 0; 
	pcErrorCode error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandExecuteToDrawCall(bool stopOnGLError)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_EXECUTE (DBG_JUMP_TO_DRAW_CALL)\n");
    rec->operation = DBG_EXECUTE;
	rec->items[0] = DBG_JUMP_TO_DRAW_CALL;
	rec->items[1] = stopOnGLError ? 1 : 0; 
	pcErrorCode error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandExecuteToShaderSwitch(bool stopOnGLError)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_EXECUTE (DBG_JUMP_TO_SHADER_SWITCH)\n");
    rec->operation = DBG_EXECUTE;
	rec->items[0] = DBG_JUMP_TO_SHADER_SWITCH;
	rec->items[1] = stopOnGLError ? 1 : 0; 
	pcErrorCode error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandExecuteToUserDefined(const char *fname,
                                                           bool stopOnGLError)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_EXECUTE (DBG_JUMP_TO_USER_DEFINED)\n");
    rec->operation = DBG_EXECUTE;
	rec->items[0] = DBG_JUMP_TO_USER_DEFINED;
	rec->items[1] = stopOnGLError ? 1 : 0; 
	strncpy(rec->fname, fname, SHM_MAX_FUNCNAME);
	pcErrorCode error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandDone()
{
	pcErrorCode error;

    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_DONE\n");
    rec->operation = DBG_DONE;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

pcErrorCode ProgramControl::dbgCommandGetBackTrace(backtrace_stringdata_t& btdata)
{
	btdata.clear();
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    dbgPrint(DBGLVL_INFO, "send: DBG_BACKTRACE\n");
    rec->operation = DBG_BACKTRACE;
	pcErrorCode error = executeDbgCommand();
	if( error == PCE_NONE )
	{
		assert( rec->result == DBG_RETURN_BACKTRACE );

		int num_frames = (ALIGNED_DATA) rec->items[0];

		printf( "SUP recieved backtrace num_frames<%d>\n", int(num_frames) );

		for( int iframe=0; iframe<num_frames; iframe++ )
		{	int idest = 1+(iframe*1024);
			auto raw_frame_text = (const char*) & rec->items[idest];
	    	ork::fxstring<1024> stack_frame_text;
	    	stack_frame_text.format( "#%03i: %s", iframe, raw_frame_text );

	    	btdata.push_back(stack_frame_text.c_str());

	    	printf( "%s\n", stack_frame_text.c_str() );
		}

	}
	return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandCallOrig()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_CALL_ORIGFUNCTION\n");
    rec->operation = DBG_CALL_ORIGFUNCTION;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
    	printResult();
	}
    return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandSetDbgTarget(int target, int alphaTestOption,
		                                           int depthTestOption, int stencilTestOption,
                                                   int blendingOption)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_SET_DBG_TARGET\n");
    rec->operation = DBG_SET_DBG_TARGET;
	rec->items[0] = target;
	rec->items[1] = alphaTestOption;
	rec->items[2] = depthTestOption;
	rec->items[3] = stencilTestOption;
	rec->items[4] = blendingOption;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandRestoreRenderTarget(int target)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_RESTORE_RENDER_TARGET\n");
    rec->operation = DBG_RESTORE_RENDER_TARGET;
	rec->items[0] = target;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandSaveAndInterruptQueries(void)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_SAVE_AND_INTERRUPT_QUERIES\n");
    rec->operation = DBG_SAVE_AND_INTERRUPT_QUERIES;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandRestartQueries(void)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_RESTART_QUERIES\n");
    rec->operation = DBG_RESTART_QUERIES;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}
	
////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandStartRecording()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_START_RECORDING\n");
    rec->operation = DBG_START_RECORDING;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
    dbgPrint(DBGLVL_INFO, "function call recording done!\n");
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandReplay(int target)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_REPLAY\n");
    rec->operation = DBG_REPLAY;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandEndReplay()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_END_REPLAY\n");
    rec->operation = DBG_END_REPLAY;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandRecord()
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_RECORD_CALL\n");
    rec->operation = DBG_RECORD_CALL;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
	    printResult();
	}
    return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandCallOrig(const FunctionCall *fCall)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;
    int i;
    
    if (!rec) {
        dbgPrint(DBGLVL_ERROR, "no rec\n");
        exit(1);
    }

    if (strcmp(rec->fname, fCall->getName()) != 0) {
        dbgPrint(DBGLVL_ERROR, "function name does not match record\n");
        exit(1);
    }

    rec->numItems = fCall->getNumArguments();

    dbgPrint(DBGLVL_INFO, "send: DBG_CALL_ORIGFUNCTION_WITH_CHANGED_ARGUMENTS\n");
    rec->operation = DBG_CALL_ORIGFUNCTION;

    for (i = 0; i < fCall->getNumArguments(); i++) {
        rec->items[2*i+1] = fCall->getArgument(i)->iType;
        dbgPrint(DBGLVL_INFO, "argument %f\n", *(float*)fCall->getArgument(i)->pData);
        copyArgumentToProcess(fCall->getArgument(i)->pAddress,
                fCall->getArgument(i)->pData,  fCall->getArgument(i)->iType);
    }
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
	    printResult();
	}
    return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::overwriteFuncArguments(const FunctionCall *fCall)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    int i;

    if (!rec) {
        dbgPrint(DBGLVL_ERROR, "no rec\n");
        exit(1);
    }
    
    if (strcmp(rec->fname, fCall->getName()) != 0) {
        dbgPrint(DBGLVL_ERROR, "function name does not match record\n");
        exit(1);
    }

    dbgPrint(DBGLVL_INFO, "change: NEW_ARGUMENTS_FOR_ORIGINAL_FUNCTION\n");
    for (i = 0; i < fCall->getNumArguments(); i++) {
        dbgPrint(DBGLVL_INFO, "argument %f\n", *(float*)fCall->getArgument(i)->pData);
        copyArgumentToProcess(fCall->getArgument(i)->pAddress,
                fCall->getArgument(i)->pData,  fCall->getArgument(i)->iType);
    }
    return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandCallDBGFunction(const char* dbgFunctionName)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
   	pcErrorCode error;

    if (dbgFunctionName) {
        strcpy(rec->fname, dbgFunctionName);
    }
    
    dbgPrint(DBGLVL_INFO, "send: DBG_CALL_FUNCTION\n");
    rec->operation = DBG_CALL_FUNCTION;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
    dbgPrint(DBGLVL_INFO, "dbg function %s called for \"%s\"!\n", 
            dbgFunctionName, rec->fname);
    return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandFreeMem(unsigned int numBlocks, void **addresses)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	unsigned int i;
	pcErrorCode error;

	if (numBlocks > SHM_MAX_ITEMS) {
		dbgPrint(DBGLVL_ERROR, "ProgramControl::dbgCommandFreeMem: Cannot free "
		                "%i memory blocks in one call! Max. is %i!\n",
		        numBlocks, (int)SHM_MAX_ITEMS);
		exit(1);
	}
	
    dbgPrint(DBGLVL_INFO, "send: DBG_FREE_MEM\n");
    rec->operation = DBG_FREE_MEM;
	rec->numItems = numBlocks;
	for (i = 0; i < numBlocks; i++) {
		rec->items[i] = (ALIGNED_DATA)addresses[i];
	}
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	/* TODO: debug message */
	if (error == PCE_NONE) {
		for (i = 0; i < numBlocks; i++) {
    		dbgPrint(DBGLVL_INFO, "ProgramControl::dbgCommandFreeMem: memory free'd at %p\n", addresses[i]);
		}
	} else {
		dbgPrint(DBGLVL_WARNING, "ProgramControl::dbgCommandFreeMem: error free'ing memory\n");
	}
    return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandAllocMem(unsigned int numBlocks,
		                                       unsigned int *sizes,
                                               void **addresses)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	unsigned int i;
	pcErrorCode error;
	
	if (numBlocks > SHM_MAX_ITEMS) {
		dbgPrint(DBGLVL_ERROR, "ProgramControl::dbgCommandAllocMem: Cannot allocate "
		                "%i memory blocks in one call! Max. is %i!\n",
		        numBlocks, (int)SHM_MAX_ITEMS);
		exit(1);
	}

    dbgPrint(DBGLVL_INFO, "send: DBG_ALLOC_MEM\n");
    rec->operation = DBG_ALLOC_MEM;
	rec->numItems = numBlocks;
	for (i = 0; i < numBlocks; i++) {
		rec->items[i] = sizes[i];
	}
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	if (rec->result == DBG_ALLOCATED) {
		for (i = 0; i < numBlocks; i++) {
			addresses[i] = (void*)rec->items[i];
			dbgPrint(DBGLVL_INFO, "%i bytes of memory allocated at %p\n",
					sizes[i], addresses[i]);
		}
		return PCE_NONE;
	} else {
    	return checkError();
	}
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandClearRenderBuffer(int mode,
                                                        float r, float g,
                                                        float b, float a,
                                                        float f, int s)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_CLEAR_RENDER_BUFFER\n");
    rec->operation = DBG_CLEAR_RENDER_BUFFER;
	rec->items[0] = (ALIGNED_DATA)mode;
	*(float*)(void*)&rec->items[1] = r;
	*(float*)(void*)&rec->items[2] = g;
	*(float*)(void*)&rec->items[3] = b;
	*(float*)(void*)&rec->items[4] = a;
	*(float*)(void*)&rec->items[5] = f;
	rec->items[6] = (ALIGNED_DATA)s;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandReadRenderBuffer(int numComponents, 
                                                       int *width, int *height,
                                                       float **image)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;

    dbgPrint(DBGLVL_INFO, "send: DBG_READ_RENDER_BUFFER\n");
    rec->operation = DBG_READ_RENDER_BUFFER;
	rec->items[0] = (ALIGNED_DATA)numComponents;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
		void *buffer = (void*)rec->items[0];
		*width = (int)rec->items[1];
		*height = (int)rec->items[2];
		/* TODO: check error */
		*image = (float*)malloc(numComponents*(*width)*(*height)*sizeof(float));
    	cpyFromProcess(debuggedProgramPID, *image, buffer,
		               numComponents*(*width)*(*height)*sizeof(float));
		error = dbgCommandFreeMem(1, &buffer);
	}
	return error;
}
	
////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandShaderStepFragment(void *shaders[3],
                                                         int numComponents,
                                                         int format,
                                                         int *width, int *height,
                                                         void **image)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;
	
    dbgPrint(DBGLVL_INFO, "send: DBG_SHADER_STEP\n");
    rec->operation = DBG_SHADER_STEP;
	rec->items[0] = (ALIGNED_DATA)shaders[0];
	rec->items[1] = (ALIGNED_DATA)shaders[1];
	rec->items[2] = (ALIGNED_DATA)shaders[2];
	rec->items[3] = (ALIGNED_DATA)DBG_TARGET_FRAGMENT_SHADER;
	rec->items[4] = (ALIGNED_DATA)numComponents;
	rec->items[5] = (ALIGNED_DATA)format;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
		if (rec->result == DBG_READBACK_RESULT_FRAGMENT_DATA) {
			void *buffer = (void*)rec->items[0];
			*width = (int)rec->items[1];
			*height = (int)rec->items[2];
			if (!buffer || *width <= 0 || *height <= 0) {
				error = PCE_DBG_INVALID_VALUE;
			} else {
				int formatSize;
				switch (format) {
					case GL_FLOAT:
						formatSize = sizeof(float);
						break;
					case GL_INT:
						formatSize = sizeof(int);
						break;
					case GL_UNSIGNED_INT:
						formatSize = sizeof(unsigned int);
						break;
					default:
						return PCE_DBG_INVALID_VALUE;
				}

				*image = malloc(numComponents*(*width)*(*height)*formatSize);

    			cpyFromProcess(debuggedProgramPID, *image, buffer,
			    	           numComponents*(*width)*(*height)*formatSize);
				error = dbgCommandFreeMem(1, &buffer);
			}
		} else {
			error = PCE_DBG_INVALID_VALUE;
		}
	}
	return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::dbgCommandShaderStepVertex(void *shaders[3],
                                                       int target,
                                                       int primitiveMode,
													   int forcePointPrimitiveMode,
													   int numFloatsPerVertex,
                                                       int *numPrimitives,
                                                       int *numVertices,
                                                       float **vertexData)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	pcErrorCode error;
	
    dbgPrint(DBGLVL_INFO, "send: DBG_SHADER_STEP\n");
    rec->operation = DBG_SHADER_STEP;
	rec->items[0] = (ALIGNED_DATA)shaders[0];
	rec->items[1] = (ALIGNED_DATA)shaders[1];
	rec->items[2] = (ALIGNED_DATA)shaders[2];
	rec->items[3] = (ALIGNED_DATA)target;
	rec->items[4] = (ALIGNED_DATA)primitiveMode;
	rec->items[5] = (ALIGNED_DATA)forcePointPrimitiveMode;
	rec->items[6] = (ALIGNED_DATA)numFloatsPerVertex;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	error = checkError();
	if (error == PCE_NONE) {
		if (rec->result == DBG_READBACK_RESULT_VERTEX_DATA) {
			void *buffer = (void*)rec->items[0];
			*numVertices = (int)rec->items[1];
			*numPrimitives = (int)rec->items[2];
			*vertexData = (float*)malloc(*numVertices*numFloatsPerVertex*sizeof(float));
    		cpyFromProcess(debuggedProgramPID, *vertexData, buffer,
		    	           *numVertices*numFloatsPerVertex*sizeof(float));
			error = dbgCommandFreeMem(1, &buffer);
		} else {
			error = PCE_DBG_INVALID_VALUE;
		}
	}
	return error;
}

///////////////////////////////////////////////////////////////////////////////
// IPCQ recieve from inferior
///////////////////////////////////////////////////////////////////////////////

void ProgramControl::IpcqRecvThreadImpl()
{
	bool bdone = false;

	while( false == bdone )
	{
		ork::NetworkMessage msg;
		while( mReciever->try_recv( msg ) )
		{
			ork::NetworkMessageIterator iter( msg );

			EIPCDBG_INF_TO_SUP enu = EIPCMSG_I2S_END;
			msg.Read(enu,iter);

			switch(enu)
			{
				case EIPCMSG_I2S_GENERAL:
				{	std::string read_str = msg.ReadString(iter);
					printf( "SUP>>ipcqrecvr recieved message from INF (%s)\n", read_str.c_str() );
					ork::NetworkMessage ret_msg;
					ret_msg.Read(enu,iter);
					EIPCDBG_SUP_TO_INF out_enu = EIPCMSG_S2I_GENERAL;
					ret_msg.Write(out_enu);
					ork::fxstring<2048> out_str;
					out_str.format("ACK:%s", read_str.c_str());
					ret_msg.WriteString(out_str.c_str());
					mSender->send(ret_msg);
					break;
				}
				default:
					assert(false);
					break;
			}


			// echo it back! (as a test)


		}
		usleep(100000);
	}
}

///////////////////////////////////////////////////////////////////////////////
// launch inferior
///////////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::runProgram(char **debuggedProgramArgs, char *workDir)
{
	pcErrorCode error;

    clearShmem();

    mSender = new ork::IpcMsgQSender;
    mReciever = new ork::IpcMsgQReciever;

    debuggedProgramPID = vfork();

    ////////////////////////////////////////
    // vfork error?
    ////////////////////////////////////////
    if (debuggedProgramPID < 0)
    {
	    dbgPrint(DBGLVL_INFO, "SUP>>vfork error: %i\n", debuggedProgramPID);
		debuggedProgramPID = 0;
        return PCE_FORK;
    }
    ////////////////////////////////////////
    // vfork child branch
    ////////////////////////////////////////
    else if (debuggedProgramPID == 0)
    {
        setDebugEnvVars();
        
        if (workDir != NULL) {
            chdir(workDir);
            // TODO: Error checking here?
        }

        dbgPrint(DBGLVL_INFO, "INF>>vfork executing %s\n", debuggedProgramArgs[0]);
        ptrace(PTRACE_TRACEME, 0, 0, 0);
		/*
		ptrace ((__ptrace_request)PTRACE_SETOPTIONS, getpid(), 0,
				PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
				PTRACE_O_TRACEEXEC | PTRACE_O_TRACEVFORKDONE |
				PTRACE_O_TRACECLONE);
		*/
        setpgid (0, 0);
        execv(debuggedProgramArgs[0], debuggedProgramArgs);

		/* an error occurd, execv should never return */
		dbgPrint(DBGLVL_ERROR, "INF>>execution failed: %s\n", strerror(errno));
		dbgPrint(DBGLVL_INFO, "INF>>My pid: %i\n", getpid());
        _exit(73);
    }

    ////////////////////////////////////////
    // vfork parent branch
    ////////////////////////////////////////

    ////////////////////////////////////////
    // start IPCQ's

    ork::fxstring<256> sendr_name, recvr_name;

	sendr_name.format( "glsld_send<%d>", debuggedProgramPID );
	recvr_name.format( "glsld_recv<%d>", debuggedProgramPID );

    mSender->Create(sendr_name.c_str());
    mReciever->Create(recvr_name.c_str());

    auto recv_thread_impl = [=]()
    {	this->IpcqRecvThreadImpl();
    };

    ork::thread* recv_thread = new ork::thread;

    recv_thread->start( recv_thread_impl );

    ////////////////////////////////////////

    // enable ptracing

#if 0
    ptrace((__ptrace_request)PTRACE_SETOPTIONS, debuggedProgramPID, 0, 0);
#else
	ptrace ((__ptrace_request)PTRACE_SETOPTIONS, debuggedProgramPID, 0,
	        PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
	        PTRACE_O_TRACEEXEC | PTRACE_O_TRACEVFORKDONE /*|
	        PTRACE_O_TRACECLONE*/);
#endif

    dbgPrint(DBGLVL_INFO, "SUP>>wait for child\n");
	error = checkChildStatus();
	if (error != PCE_NONE) {
		kill(debuggedProgramPID, SIGKILL);
		debuggedProgramPID = 0;
		return error;
	}
    dbgPrint(DBGLVL_INFO, "SUP>>send continue\n");
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		kill(debuggedProgramPID, SIGKILL);
		debuggedProgramPID = 0;
		return error;
	}
#ifdef DEBUG
    printCall();
#endif
    return PCE_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// attach to inferior
///////////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::attachToProgram(const pid_t pid) {
    return PCE_UNKNOWN_ERROR;
}

FunctionCall* ProgramControl::getCurrentCall(void)
{
    FunctionCall *fCall = new FunctionCall();
    int i;
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    
    if (!rec) {
        dbgPrint(DBGLVL_ERROR, "no rec\n");
        exit(1);
    }
    
    fCall->setName(rec->fname);
    
    for (i = 0; i < (int)rec->numItems; i++) {
        fCall->addArgument(rec->items[2*i+1], 
            copyArgumentFromProcess((void*)rec->items[2*i], rec->items[2*i+1]),
            (void*)rec->items[2*i]);
    }

    return fCall;
}

///////////////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::getShaderCode(char *shaders[3],
                                          TBuiltInResource *resource,
										  char **serializedUniforms,
										  int *numUniforms)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    int i;
	pcErrorCode error;
	void *addr[5];

	sched_yield();

    dbgPrint(DBGLVL_INFO, "send: DBG_GET_SHADER_CODE\n");
    rec->operation = DBG_GET_SHADER_CODE;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
    error = checkError();
    if (error != PCE_NONE) {
        return error;
    }
	
	for (i = 0; i < 3; i++) {
		shaders[i] = NULL;
	}

	if (rec->result != DBG_SHADER_CODE) {
		return checkError();
	}

    if (rec->numItems > 0) {


		addr[0]  = (void*)rec->items[0];
		addr[1]  = (void*)rec->items[2];
		addr[2]  = (void*)rec->items[4];
		addr[3]  = (void*)rec->items[6];
		addr[4]  = (void*)rec->items[9];

		/* copy shader sources */
        for (i=0; i<3; i++) {
            if (rec->items[2*i] == 0) {
                continue;
            }
            if (!(shaders[i] = new char[rec->items[2*i+1]+1])) {
                dbgPrint(DBGLVL_ERROR, "not enough memory\n");
				for (--i; i >= 0; i--) {
					delete [] shaders[i];
					shaders[i] = NULL;
				}
				/* TODO: what about memory on client side? */
				error = dbgCommandFreeMem(5, addr);
				return PCE_MEMORY_ALLOCATION_FAILED;
            }
            cpyFromProcess(debuggedProgramPID, shaders[i], addr[i], rec->items[2*i+1]);
			shaders[i][rec->items[2*i+1]] = '\0';
        }

		/* copy shader resource info */
		cpyFromProcess(debuggedProgramPID, resource, addr[3],
		               sizeof(TBuiltInResource));

		if (rec->items[7] > 0) {
			*numUniforms = rec->items[7];
            if (!(*serializedUniforms = new char[rec->items[8]])) {
                dbgPrint(DBGLVL_ERROR, "not enough memory\n");
				for (i = 0; i < 3; ++i) {
					delete [] shaders[i];
					shaders[i] = NULL;
					*serializedUniforms = NULL;
					*numUniforms = 0;
				}
				/* TODO: what about memory on client side? */
				error = dbgCommandFreeMem(5, addr);
				return PCE_MEMORY_ALLOCATION_FAILED;
            }
			cpyFromProcess(debuggedProgramPID, *serializedUniforms,
					addr[4], rec->items[8]);
		}
		else
		{
			*serializedUniforms = NULL;
			*numUniforms = 0;
		}

        /* free memory on client side */
		dbgPrint(DBGLVL_INFO, "getShaderCode: free memory on client side [%p, %p, %p, %p, %p]\n",
				addr[0], addr[1], addr[2], addr[3], addr[4]);
		error = dbgCommandFreeMem(5, addr);
		if (error != PCE_NONE) {
			dbgPrint(DBGLVL_WARNING, "getShaderCode: free memory on client side error: %i\n", error);
			for (i=0; i<3; i++) {
				delete [] shaders[i];
				shaders[i] = NULL;
			}
			return error;
		}
	}
	dbgPrint(DBGLVL_INFO, ">>>>>>>>> Orig. Vertex Shader <<<<<<<<<<<\n%s\n"
	                      ">>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<\n",
	         shaders[0]); 
	dbgPrint(DBGLVL_INFO, ">>>>>>>> Orig. Geometry Shader <<<<<<<<<<\n%s\n"
	                      ">>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<\n",
	         shaders[1]); 
	dbgPrint(DBGLVL_INFO, ">>>>>>>> Orig. Fragment Shader <<<<<<<<<<\n%s\n"
	                      ">>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<\n",
	         shaders[2]); 
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::saveActiveShader(void)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

	sched_yield();
	
    dbgPrint(DBGLVL_INFO, "send: DBG_STORE_ACTIVE_SHADER\n");
    rec->operation = DBG_STORE_ACTIVE_SHADER;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::restoreActiveShader(void)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
    pcErrorCode error;

	sched_yield();

	
    dbgPrint(DBGLVL_INFO, "send: DBG_RESTORE_ACTIVE_SHADER\n");
    rec->operation = DBG_RESTORE_ACTIVE_SHADER;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		return error;
	}
	return checkError();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::setDbgShaderCode(char *shaders[3], int target)
{
	DbgRec *rec = getThreadRecord(debuggedProgramPID);
	int i;
	pcErrorCode error;
	void *addr[3];

	sched_yield();

	dbgPrint(DBGLVL_INFO, "ProgramControl::setDbgShaderCode: %p, %p, %p, %i\n",
			shaders[0], shaders[1], shaders[2], target);

	/* allocate client side memory and copy shader src */
	for (i = 0; i < 3; i++) {
		if (shaders[i]) {
			unsigned int size = strlen(shaders[i]) + 1;
			dbgPrint(DBGLVL_ERROR, "allocate memory for shader[%i]: %ibyte\n", i, size);
			error = dbgCommandAllocMem(1, &size, &addr[i]);
			if (error != PCE_NONE) {
				dbgCommandFreeMem(i, addr);
				return error;
			}
			cpyToProcess(debuggedProgramPID, addr[i], shaders[i], size);
		} else {
			addr[i] = NULL;
		}
	}
	dbgPrint(DBGLVL_INFO, "send: DBG_SET_DBG_SHADER\n");
	for (i = 0; i < 3; i++) {
		rec->items[i] = (ALIGNED_DATA)addr[i];
	}
	rec->operation = DBG_SET_DBG_SHADER;
	rec->items[3] = target;
	error = executeDbgCommand();
	if (error != PCE_NONE) {
		dbgCommandFreeMem(3, addr);
		return error;
	}
	error = checkError();
	if (error != PCE_NONE) {
		dbgCommandFreeMem(3, addr);
		return error;
	}
	
	/* free memory on client side */
	dbgPrint(DBGLVL_INFO, "setShaderCode: free memory on client side [%p, %p, %p]\n",
			addr[0], addr[1], addr[2]);
	error = dbgCommandFreeMem(3, addr);
	if (error != PCE_NONE) {
		dbgPrint(DBGLVL_ERROR, "getShaderCode: free memory on client side error: %i\n", error);
		return error;
	}
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::initializeRenderBuffer(bool copyRGB,
                                 bool copyAlpha, bool copyDepth,
                                 bool copyStencil, float red, float green,
                                 float blue, float alpha, float depth, int stencil)
{
	int mode = DBG_CLEAR_NONE;
	
	sched_yield();
	
	if (!copyRGB) {
		mode |= DBG_CLEAR_RGB;
	}
	if (!copyAlpha) {
		mode |= DBG_CLEAR_ALPHA;
	}
	if (!copyDepth) {
		mode |= DBG_CLEAR_DEPTH;
	}
	if (!copyStencil) {
		mode |= DBG_CLEAR_STENCIL;
	}
	return dbgCommandClearRenderBuffer(mode, red, green, blue, alpha, depth, stencil);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::readBackActiveRenderBuffer(int numComponents,
                                                       int *width, int *heigh,
                                                       float **image)
{
	sched_yield();

	return dbgCommandReadRenderBuffer(numComponents, width, heigh, image);
}


////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::insertGlEnd(void)
{
	sched_yield();

    return dbgCommandCallDBGFunction("glEnd");
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::callOrigFunc(const FunctionCall *fCall)
{
	sched_yield();

	printf( "callOrigFunc fcall<%p>\n", fCall );

    if (fCall) {
        return dbgCommandCallOrig(fCall);
    } else {
        return dbgCommandCallOrig();
    }
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::getBackTrace(backtrace_stringdata_t&btdata)
{
	sched_yield();
    return dbgCommandGetBackTrace(btdata);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::restoreRenderTarget(int target)
{
	sched_yield();
	
	return dbgCommandRestoreRenderTarget(target);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::setDbgTarget(int target, int alphaTestOption,
                                         int depthTestOption, int stencilTestOption,
                                         int blendingOption)
{
	sched_yield();
	
	return dbgCommandSetDbgTarget(target, alphaTestOption, depthTestOption,
	                              stencilTestOption, blendingOption);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::saveAndInterruptQueries(void)
{
	sched_yield();
	
	return dbgCommandSaveAndInterruptQueries();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::restartQueries(void)
{
	sched_yield();
	
	return dbgCommandRestartQueries();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::initRecording()
{
	sched_yield();

    return dbgCommandStartRecording();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::recordCall()
{
	sched_yield();

    return dbgCommandRecord();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::replay(int target)
{
	sched_yield();

    return dbgCommandReplay(target);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::endReplay()
{
	sched_yield();

    return dbgCommandEndReplay();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::shaderStepFragment(char *shaders[3],
                                               int numComponents, int format,
                                               int *width, int *heigh, void **image)
{
	pcErrorCode error;
	void *addr[3];
	int i;

	sched_yield();

	/* allocate client side memory and copy shader src */
	for (i = 0; i < 3; i++) {
		if (shaders[i]) {
			unsigned int size = strlen(shaders[i]) + 1;
			error = dbgCommandAllocMem(1, &size, &addr[i]);
			if (error != PCE_NONE) {
				dbgCommandFreeMem(i, addr);
				return error;
			}
			cpyToProcess(debuggedProgramPID, addr[i], shaders[i], size);
		} else {
			addr[i] = NULL;
		}
	}
	
	error = dbgCommandShaderStepFragment(addr, numComponents, format, width, heigh, image);
	if (error) {
		dbgCommandFreeMem(3, addr);
		return error;
	}
	
	/* free memory on client side */
	error = dbgCommandFreeMem(3, addr);
	if (error) {
		dbgPrint(DBGLVL_ERROR, "getShaderCode: free memory on client side error: %i\n", error);
	}
	return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::shaderStepVertex(char *shaders[3], int target,
                                             int primitiveMode,
											 int forcePointPrimitiveMode,
											 int numFloatsPerVertex,
                                             int *numPrimitives,
                                             int *numVertices,
                                             float **vertexData)
{
	pcErrorCode error;
	void *addr[3];
	int i, basePrimitiveMode;
	
	sched_yield();

	/* allocate client side memory and copy shader src */
	for (i = 0; i < 3; i++) {
		if (shaders[i]) {
			unsigned int size = strlen(shaders[i]) + 1;
			error = dbgCommandAllocMem(1, &size, &addr[i]);
			if (error != PCE_NONE) {
				dbgCommandFreeMem(i, addr);
				return error;
			}
			cpyToProcess(debuggedProgramPID, addr[i], shaders[i], size);
		} else {
			addr[i] = NULL;
		}
	}

	switch (primitiveMode) {
		case GL_POINTS:
			basePrimitiveMode = GL_POINTS;
			break;
		case GL_LINES:
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
		case GL_LINES_ADJACENCY_EXT:
		case GL_LINE_STRIP_ADJACENCY_EXT:
			basePrimitiveMode = GL_LINES;
			break;
		case GL_TRIANGLES:
		case GL_QUADS:
		case GL_QUAD_STRIP:
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
		case GL_TRIANGLES_ADJACENCY_EXT:
		case GL_TRIANGLE_STRIP_ADJACENCY_EXT:
		case GL_QUAD_MESH_SUN:
		case GL_TRIANGLE_MESH_SUN:
			basePrimitiveMode = GL_TRIANGLES;
			break;
		default:
			dbgPrint(DBGLVL_WARNING, "Unknown primitive mode\n");
			return PCE_DBG_INVALID_VALUE;
	}
	
	error = dbgCommandShaderStepVertex(addr, target, basePrimitiveMode,
	                                   forcePointPrimitiveMode, numFloatsPerVertex,
	                                   numPrimitives, numVertices, vertexData);
	if (error) {
		dbgCommandFreeMem(3, addr);
		return error;
	}
	
	/* free memory on client side */
	error = dbgCommandFreeMem(3, addr);
	if (error) {
		dbgPrint(DBGLVL_WARNING, "getShaderCode: free memory on client side error: %i\n", error);
	}
	return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::callDone(void)
{
	pcErrorCode error;
	
	sched_yield();

    error = dbgCommandDone();
#ifdef DEBUG
    printCall();
#endif
	return error;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::execute(bool stopOnGLError)
{
	sched_yield();
	return dbgCommandExecute(stopOnGLError);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::executeToShaderSwitch(bool stopOnGLError)
{
	sched_yield();
	return dbgCommandExecuteToShaderSwitch(stopOnGLError);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::executeToDrawCall(bool stopOnGLError)
{
	sched_yield();
	return dbgCommandExecuteToDrawCall(stopOnGLError);
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::executeToUserDefined(const char *fname,
                                                 bool stopOnGLError)
{
	sched_yield();
	return dbgCommandExecuteToUserDefined(fname, stopOnGLError);

}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::stop(void)
{
	sched_yield();
	return dbgCommandStopExecution();
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::checkExecuteState(int *state)
{
    DbgRec *rec = getThreadRecord(debuggedProgramPID);
	dbgPrint(DBGLVL_INFO, "ProgramControl::checkExecuteState: %i\n", (int)rec->result);
	switch (rec->result) {
		case DBG_EXECUTE_IN_PROGRESS:
			*state = 0;
			return PCE_NONE;
		case DBG_FUNCTION_CALL:
			*state = 1;
			return PCE_NONE;
		case DBG_ERROR_CODE:
			*state = 1;
			return checkError();
		default:
			return PCE_UNKNOWN_ERROR;
	}
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::executeContinueOnError(void)
{
	sched_yield();
    ptrace(PTRACE_CONT, debuggedProgramPID, 0, 0);
	return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::killProgram(int hard)
{
	dbgPrint(DBGLVL_INFO, "CHILD KILLED: %i\n", hard);
	if (debuggedProgramPID) {
		if (hard) {
			kill(debuggedProgramPID, SIGKILL);
		} else {
	        ptrace(PTRACE_KILL, debuggedProgramPID, 0, 0);
		}
       	debuggedProgramPID = 0;
		return checkChildStatus();
    }
    return PCE_NONE;
}

////////////////////////////////////////////////////////////////////////

pcErrorCode ProgramControl::detachFromProgram(void)
{
    pcErrorCode retval = PCE_UNKNOWN_ERROR;

    printf( "detaching from pid<%d>\n", debuggedProgramPID );

   	if( debuggedProgramPID )
	{	ptrace(PTRACE_DETACH, debuggedProgramPID, NULL, NULL);    // TODO
    	retval = PCE_EXIT;
		this->stop();   // Ensure consistent state.
        this->checkChildStatus();
    }
    return retval;
}

////////////////////////////////////////////////////////////////////////

void ProgramControl::initShmem(void)
{
    shmid = shmget(IPC_PRIVATE, SHM_SIZE, SHM_R | SHM_W);

    if (shmid == -1) {
        dbgPrint(DBGLVL_ERROR, "Creation of shared mem segment failed %s\n", strerror(errno));
        exit(1);
    }

    fcalls = (DbgRec*) shmat(shmid, NULL, 0);

    if ((void*)fcalls == (void*)-1) {
        dbgPrint(DBGLVL_ERROR, "Attaching to shared mem segment failed: %s\n", strerror(errno));
        exit(1);
    }
}

////////////////////////////////////////////////////////////////////////

void ProgramControl::clearShmem(void)
{
    memset(fcalls, 0, SHM_SIZE);
}

////////////////////////////////////////////////////////////////////////

void ProgramControl::freeShmem(void)
{
    shmctl(shmid, IPC_RMID, 0);

    if (shmdt(fcalls) == -1) {
        dbgPrint(DBGLVL_ERROR, "Deleting shared mem segment failed: %s\n", strerror(errno));
    }
}

