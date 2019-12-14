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

#include <signal.h>

#ifndef SRC_LIBCHILD_H_
#define SRC_LIBCHILD_H_

#define LIBCHILD_H_EXPORT_FUNCTION __attribute__((visibility("default")))

struct LibChild;
typedef struct LibChild LibChild;

struct Child;
typedef struct Child Child;

enum childStates {
    CHILD_STARTING = 0,
    CHILD_STARTED = 1,
    CHILD_TERMINATED = 2
};

LIBCHILD_H_EXPORT_FUNCTION LibChild* libChildCreateWorker(char* slaveName, char* userName,
                                                  void(*signalReceived)(siginfo_t signal));
LIBCHILD_H_EXPORT_FUNCTION void      libChildKill(Child* child, int signalId);
LIBCHILD_H_EXPORT_FUNCTION Child*    libChildExec(LibChild* lib, char* program, char* username,
                                                  char** argv, char** env,
                                                  void(*stateChange)(Child* child, void* param, enum childStates state),
                                                  void(*childData)(Child* child, void* param, char* buffer, size_t len, int isErr),
                                                  void* param);
LIBCHILD_H_EXPORT_FUNCTION int       libChildExitStatus(Child* child);
LIBCHILD_H_EXPORT_FUNCTION void      libChildFreeHandle(Child* child);
LIBCHILD_H_EXPORT_FUNCTION int       libChildPoll(LibChild* lib);
LIBCHILD_H_EXPORT_FUNCTION int       libChildGetFd(LibChild* lib);
LIBCHILD_H_EXPORT_FUNCTION void      libChildMain();
LIBCHILD_H_EXPORT_FUNCTION void      libChildTerminateWorker(LibChild* lib);


#endif /* SRC_LIBCHILD_H_ */
