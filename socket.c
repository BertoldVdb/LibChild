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

#include "def.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#ifdef __linux__
#define SEND_FLAGS MSG_NOSIGNAL
#else
#define SEND_FLAGS 0
#endif

static void setNonBlock(int fd, int on){
    int flags = fcntl(fd, F_GETFL, 0);

    if(on){
        flags |=  O_NONBLOCK;
    }else{
        flags &=~ O_NONBLOCK;
    }

    fcntl(fd, F_SETFL, flags);
}

int libChildReadFull(int fd, char* buffer, size_t len, int unblock)
{
    int first = 1;

    while(len) {
        int doBlock = 0;
        if(first && unblock){
            setNonBlock(fd, 1);
            doBlock = 1;
        }
        
        ssize_t bytesRead = read(fd, buffer, len);

        if(doBlock){
            setNonBlock(fd, 0);
        }

        if(bytesRead < 0) {
            if(errno == EINTR) {
                continue;
            }
            if(errno == EAGAIN) {
                return 1;
            }
            return -1;
        }
        if(bytesRead == 0){
            return -1;
        }
        len -= bytesRead;
        buffer += bytesRead;
        first = 0;
    }

    return 0;
}

int libChildWriteFull(struct LibChild* lib, int fd, char* buffer, size_t len)
{
    while(len) {
        if(lib){
            setNonBlock(fd, 1);
        }
        ssize_t bytesWritten = send(fd, buffer, len, SEND_FLAGS);
        if(lib){
            setNonBlock(fd, 0);
        }

        if(bytesWritten < 0) {
            if(errno == EINTR) {
                continue;
            }
            if(errno == EAGAIN) {
                /* We cannot write, so the buffer is likely full. This function will try to read. 
                 * In principle the call stack can grow unbounded here, if the poll causes new things to be written.
                 * This should not happen in practice */
                if(!libChildPoll(lib)){
                    continue;
                }
            }
            return -1;
        }
        if(bytesWritten == 0) {
            return -1;
        }
        len -= bytesWritten;
        buffer += bytesWritten;
    }

    return 0;
}

int libChildWriteVariable(struct LibChild* lib, int fd, void* buf, unsigned int len)
{
    if(libChildWriteFull(lib, fd, (char*)&len, sizeof(len))) return -1;
    if(libChildWriteFull(lib, fd, buf, len)) return -1;

    return 0;
}

char* libChildReadVariable(int fd, unsigned int* readLen)
{
    if(readLen) *readLen = 0;

    unsigned int len;
    if(libChildReadFull(fd, (char*)&len, sizeof(len), 0)) return NULL;

    char* buf = malloc(len+1);
    if(!buf) return buf;

    if(libChildReadFull(fd, buf, len, 0)) {
        free(buf);
        return NULL;
    }

    /* For string safety */
    buf[len] = 0;

    if(readLen) *readLen = len;
    return buf;
}

int libChildWritePack(struct LibChild* lib, int fd, char** arg)
{
    unsigned int values = 0;
    /* Search for null */
    if(arg) {
        while(arg[values]) {
            values++;
        }
    }

    if(libChildWriteFull(lib, fd, (char*)&values, sizeof(values))) return -1;

    for(unsigned int i=0; i<values; i++) {
        if(libChildWriteVariable(lib, fd, arg[i], strlen(arg[i]))) return -1;
    }

    return 0;
}

void libChildFreePack(char** arg)
{
    if(!arg) return;

    unsigned int values = 0;
    while(arg[values]) {
        free(arg[values]);
        arg[values] = NULL;
        values ++;
    }

    free(arg);
}

char** libChildReadPack(int fd)
{
    unsigned int values;
    if(libChildReadFull(fd, (char*)&values, sizeof(values), 0)) return NULL;

    unsigned int alen = (values + 1) * sizeof(char*);

    char** arg = (char**)malloc(alen);
    if(!arg) return arg;

    memset(arg, 0, alen);

    for(unsigned int i=0; i<values; i++) {
        arg[i] = libChildReadVariable(fd, NULL);
        if(!arg[i]) {
            libChildFreePack(arg);
            return NULL;
        }
    }

    return arg;
}

