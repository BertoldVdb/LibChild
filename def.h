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

#ifndef LIBCHILD_H_
#define LIBCHILD_H_

#include <unistd.h>

#include "libchild.h"

struct slaveCommand {
    int     command;
    void*   masterEcho;
    void*   paramChildProcess;
    int     paramInteger;
};

struct slaveResponse {
    void*   masterEcho;
    int     result;
    void*   paramChildProcess;
    int     paramInteger;
};

struct LibChild {
    pid_t   intermediatePid;
    int     workerDied;
    int     unusedHandle;
    int     sockets[2];
    void    (*signalReceived)(siginfo_t signal, void* param);
    void*   param;
};

typedef struct LibChild LibChild;

struct Child {
    void(*stateChange)(struct Child* child, void* param, enum childStates state);
    void(*childData)(struct Child* child, void* param, char* buffer, size_t len, int isErr);
    void* param;
    void* slaveId;
    enum childStates state;
    pid_t pid;
    LibChild* lib;
    unsigned int unusedHandle;
    int exitStatus;
};

typedef struct Child Child;

enum slaveCommands {
    SLAVE_COMMAND_EXEC = 1,
    SLAVE_COMMAND_CLOSE_HANDLE = 2,
    SLAVE_COMMAND_KILL = 3,
    SLAVE_COMMAND_EXEC_PIPE = 4,
    SLAVE_COMMAND_QUIT = 5,
};

enum slaveResults {
    SLAVE_RESULT_NULL = 0,
    SLAVE_RESULT_CHILD_CREATED = 1,
    SLAVE_RESULT_CHILD_DIED = 2,
    SLAVE_RESULT_CHILD_STDOUT_DATA = 3,
    SLAVE_RESULT_CHILD_STDERR_DATA = 4,
    SLAVE_RESULT_GOT_SIGNAL = 5
};

void libChildSlaveProcess(int socket);
int libChildReadFull(int fd, char* buffer, size_t len, int unblock);
int libChildWriteFull(struct LibChild* lib, int fd, char* buffer, size_t len);
int libChildWriteVariable(struct LibChild* lib, int fd, void* buf, unsigned int len);
char* libChildReadVariable(int fd, unsigned int* readLen);
int libChildWritePack(struct LibChild* lib, int fd, char** arg);
void libChildFreePack(char** arg);
char** libChildReadPack(int fd);

int changeUser(char* username);

#endif /* LIBCHILD_H_ */
