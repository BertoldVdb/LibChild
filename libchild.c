/* Copyright (c) 2018, Bertold Van den Bergh
 * All rights reserved.
 *
 * #Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "libchild.h"
#include "def.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

static const char* envName = "GjAG2W5xzoCarobfGY2MmA";

static char* findExecPath()
{
#ifdef __linux__
    return strdup("/proc/self/exe");
#else
#ifdef  __APPLE__
    unsigned int bufSize = 0;
    /* Find required length */
    _NSGetExecutablePath(NULL, &bufSize);

    /* Get actual value */
    char* buf = malloc(bufSize);
    if(_NSGetExecutablePath(buf, &bufSize)) {
        free(buf);
        return NULL;
    }

    return buf;
#else
#error "Not supported";
#endif
#endif
}

static void setState(Child* child, enum childStates state)
{
    child->state = state;

    if(child->unusedHandle || child->lib->unusedHandle) {
        if(child->state == CHILD_TERMINATED) {
            free(child);
        }
    } else {
        if(child->stateChange) {
            child->stateChange(child, child->param, state);
        }
    }
}

LibChild* libChildInPlace(void(*signalReceived)(siginfo_t signal, void* param), void* param){
    LibChild* lib = (LibChild*)malloc(sizeof(LibChild));

    if(lib) {
        memset(lib, 0, sizeof(*lib));
        int retVal = socketpair(AF_UNIX, SOCK_STREAM, 0, lib->sockets);

        if(retVal < 0) {
            goto fail;
        }

#ifdef __APPLE__
        {
            int set = 1;
            setsockopt(lib->sockets[0], SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(set));
        }
#endif

        lib->signalReceived = signalReceived;
        lib->param = param;
        lib->intermediatePid = fork();
        if(lib->intermediatePid < 0) {
            goto fail;
        }
        if(lib->intermediatePid > 0) {
            close(lib->sockets[0]);
            libChildSlaveProcess(lib->sockets[1]);
            _exit(EXIT_FAILURE);
        }
        close(lib->sockets[1]);
    }

    return lib;

fail:
    free(lib);
    return NULL;
}

LibChild* libChildCreateWorker(char* slaveName, char* userName, void(*signalReceived)(siginfo_t signal, void* param), void* param)
{
    LibChild* lib = (LibChild*)malloc(sizeof(LibChild));

    if(lib) {
        memset(lib, 0, sizeof(*lib));
        int retVal = socketpair(AF_UNIX, SOCK_STREAM, 0, lib->sockets);

        if(retVal < 0) {
            goto fail;
        }

#ifdef __APPLE__
        {
            int set = 1;
            setsockopt(lib->sockets[0], SOL_SOCKET, SO_NOSIGPIPE, (void *)&set, sizeof(set));
        }
#endif

        lib->signalReceived = signalReceived;
        lib->param = param;
        lib->intermediatePid = fork();
        if(!lib->intermediatePid) {
            close(lib->sockets[0]);

            char socketId[32];
            snprintf(socketId, sizeof(socketId), "%s=%u", envName, lib->sockets[1]);

            char* execPath = findExecPath();
            if(!execPath) {
                _exit (EXIT_FAILURE);
            }

            if(userName) {
                if(changeUser(userName) != 1) {
                    /* Don't exec anything unless we dropped privileges */
                    _exit (EXIT_FAILURE);
                }
            }

            char *argv[] = { slaveName, NULL };

            unsigned int i;
            for(i=0; environ[i]; i++);
            char *env[i+2];
            env[i] = socketId;
            env[i+1] = NULL;
            for(i=0; environ[i]; i++) {
                env[i] = environ[i];
            }

            execve(execPath, argv, env);

            _exit (EXIT_FAILURE);

        } else if(lib->intermediatePid < 0) {
            goto fail;
        }

        close(lib->sockets[1]);
    }

    return lib;

fail:
    free(lib);
    return NULL;
}

void libChildTerminateWorker(LibChild* lib)
{
    lib->unusedHandle = 1;

    struct slaveCommand cmd;
    cmd.command = SLAVE_COMMAND_QUIT;
    libChildWriteFull(lib, lib->sockets[0], (char*)&cmd, sizeof(cmd));

    /* Read all remaining messages */
    while(!libChildPoll(lib)) {}

    free(lib);
}

void libChildKill(Child* child, int signalId)
{
    if(child->slaveId) {
        struct slaveCommand cmd;
        cmd.command = SLAVE_COMMAND_KILL;
        cmd.paramChildProcess = child->slaveId;
        cmd.paramInteger = signalId;
        libChildWriteFull(child->lib, child->lib->sockets[0], (char*)&cmd, sizeof(cmd));
    }

    libChildPoll(child->lib);
}

Child* libChildExec(LibChild* lib, char* program, char* username, char** argv, char** env,
                    void(*stateChange)(Child* child, void* param, enum childStates state),
                    void(*childData)(Child* child, void* param, char* buffer, size_t len, int isErr),
                    void* param)
{
    Child* child = (Child*)malloc(sizeof(Child));
    if(!child) goto fail;

    memset(child, 0, sizeof(Child));

    struct slaveCommand cmd;
    if(childData) {
        cmd.command = SLAVE_COMMAND_EXEC_PIPE;
    } else {
        cmd.command = SLAVE_COMMAND_EXEC;
    }

    cmd.masterEcho = child;

    child->param = param;
    child->slaveId = NULL;
    child->stateChange = stateChange;
    child->childData = childData;
    child->lib = lib;

    if(!username) {
        username = "";
    }

    if(libChildWriteFull(lib, lib->sockets[0], (char*)&cmd, sizeof(cmd))) goto fail;
    if(libChildWriteVariable(lib, lib->sockets[0], program, strlen(program))) goto fail;
    if(libChildWriteVariable(lib, lib->sockets[0], username, strlen(username))) goto fail;
    if(libChildWritePack(lib, lib->sockets[0], argv)) goto fail;
    if(libChildWritePack(lib, lib->sockets[0], env)) goto fail;

    setState(child, CHILD_STARTING);
    
    libChildPoll(lib);
    return child;

fail:
    if(child) free(child);
    return NULL;
}

int libChildExitStatus(Child* child)
{
    return child->exitStatus;
}

void libChildFreeHandle(Child* child)
{
    if(child->state == CHILD_TERMINATED) {
        free(child);
    } else {
        child->unusedHandle = 1;
    }
}

int libChildPoll(LibChild* lib)
{
    while(1){
        struct slaveResponse resp;
        memset(&resp, 0, sizeof(resp));

        int retVal = libChildReadFull(lib->sockets[0], (char*)&resp, sizeof(resp), 1);
        if(retVal == 1){
            break;
        }else if(retVal < 0){
            goto fail;
        }

        Child* child = (Child*)resp.masterEcho;
        if(resp.result == SLAVE_RESULT_CHILD_CREATED) {
            child->pid = resp.paramInteger;
            child->slaveId = resp.paramChildProcess;
            setState(child, CHILD_STARTED);
    
        } else if(resp.result == SLAVE_RESULT_CHILD_DIED) {
            void* slaveId = child->slaveId;
            child->slaveId = NULL;
            child->exitStatus = resp.paramInteger;
    
            setState(child, CHILD_TERMINATED);

            struct slaveCommand cmd;
            cmd.command = SLAVE_COMMAND_CLOSE_HANDLE;
            cmd.paramChildProcess = slaveId;
            if(libChildWriteFull(lib, lib->sockets[0], (char*)&cmd, sizeof(cmd))) goto fail;

        } else if(resp.result == SLAVE_RESULT_CHILD_STDOUT_DATA ||
                  resp.result == SLAVE_RESULT_CHILD_STDERR_DATA) {

            unsigned int len;
            char* buffer = libChildReadVariable(lib->sockets[0], &len);
            if(!buffer) goto fail;
            if(!child->unusedHandle && !lib->unusedHandle && child->childData) {
                child->childData(child, child->param, buffer, len, resp.result == SLAVE_RESULT_CHILD_STDERR_DATA);
            }
            free(buffer);
        } else if(resp.result == SLAVE_RESULT_GOT_SIGNAL) {
            siginfo_t sigInfo;
            if(libChildReadFull(lib->sockets[0], (char*)&sigInfo, sizeof(sigInfo), 0)) goto fail;

            if(lib->signalReceived){
                lib->signalReceived(sigInfo, lib->param);
            }
        }
    }

    return 0;

fail:
    /* Could not read, so the worker process died */
    if(!lib->workerDied) {
        int status;
        waitpid(lib->intermediatePid, &status, 0);
        lib->workerDied = 1;
    }
    return -1;
}

int libChildGetFd(LibChild* lib)
{
    return lib->sockets[0];
}

#ifdef __linux__
#define GETENV secure_getenv
#else
#define GETENV getenv
#endif

__attribute__((constructor (101))) void libChildMain()
{
    char* fdStr;
    if((fdStr = GETENV(envName))) {
        int fd = atoi(fdStr);
        libChildSlaveProcess(fd);
        _exit(EXIT_FAILURE);
    }
}
