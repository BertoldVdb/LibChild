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

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "def.h"

#ifdef __linux__
#include <sys/eventfd.h>
#endif

#ifndef NSIG
#define NSIG (_SIGMAX + 1)
#endif

struct childProcess {
    struct childProcess* next;
    struct childProcess* prev;

    int    running;
    pid_t  pid;

    void*  echo;

    int    pipe_out;
    int    pipe_err;
    int    silent;

    int    status;
};

typedef struct {
    pid_t  intermediatePid;
    pid_t  grpId;
    int    chldFd[2];
    int    socket;
    struct childProcess* firstProcess;
} SlaveGlobal;

static void slaveExit(SlaveGlobal* lib)
{
    struct childProcess* it = lib->firstProcess;
    while(it) {
        if(it->pipe_out >= 0) {
            close(it->pipe_out);
        }
        if(it->pipe_err >= 0) {
            close(it->pipe_err);
        }

        if(it->running) {
            kill(it->pid, SIGKILL);
            waitpid(it->pid, &it->status, 0);

            /* Try to write something to the master, it may still be listening... */
            struct slaveResponse response;
            response.result = SLAVE_RESULT_CHILD_DIED;
            response.paramChildProcess = it;
            response.paramInteger = it->status;
            response.masterEcho = it->echo;

            libChildWriteFull(NULL, lib->socket, (char*)&response, sizeof(response));
        }
        struct childProcess* next = it->next;
        free(it);
        it = next;
    }

    close(lib->socket);
    _exit (EXIT_FAILURE);
}

static void detach(int silent)
{
    umask(0);
    int retVal = chdir("/");
    (void)retVal;

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    int fd = open("/dev/null", O_RDONLY);
    dup2(fd, STDIN_FILENO);
    close(fd);

    if(silent) {
        fd = open("/dev/null", O_RDWR);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

static void notifyDead(SlaveGlobal* lib, struct childProcess* it)
{
    if(it->running) return;
    if(!it->silent) {
        if(it->pipe_out >= 0) return;
        if(it->pipe_err >= 0) return;
    }

    struct slaveResponse response;
    response.result = SLAVE_RESULT_CHILD_DIED;
    response.paramChildProcess = it;
    response.masterEcho = it->echo;
    response.paramInteger = it->status;

    if(libChildWriteFull(NULL, lib->socket, (char*)&response, sizeof(response))) {
        slaveExit(lib);
    }
}

#define FOREACH_CHILD(lib,it)   for(struct childProcess* it = (lib)->firstProcess; it; it = it->next)

SlaveGlobal lib;

static void signalHandler(int sig, siginfo_t *siginfo, void *context)
{
    int retVal = send(lib.chldFd[1], siginfo, sizeof(*siginfo), 0);
    (void)retVal;
}

static void pipeClosed(int fd)
{
    FOREACH_CHILD(&lib, it) {
        if(it->pipe_out == fd) {
            close(it->pipe_out);
            it->pipe_out = -1;
            notifyDead(&lib, it);
            break;
        }
        if(it->pipe_err == fd) {
            close(it->pipe_err);
            it->pipe_err = -1;
            notifyDead(&lib, it);
            break;
        }
    }
}

static void setCloExec(int fd)
{
    if(fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) slaveExit(&lib);
}

void libChildSlaveProcess(int socket)
{
    /* Disconnect standard IO */
    detach(1);

    lib.firstProcess = NULL;
    lib.socket = socket;

    /* Become a session leader and create new process group */
    if(getpid() != 1){
        lib.grpId = setsid();
        if(lib.grpId < 0) {
            slaveExit(&lib);
        }
    }else{
        lib.grpId = 1;
    }

    /* Create an socket to synchronize the signals */
    if(socketpair(AF_UNIX, SOCK_DGRAM, 0, lib.chldFd)){
        slaveExit(&lib);
    }
    setCloExec(lib.chldFd[0]);
    setCloExec(lib.chldFd[1]);

    /* We have forked already once so we can safely put signal handlers */
    struct sigaction action;
    action.sa_sigaction = &signalHandler;
    action.sa_flags = SA_SIGINFO;
    for(unsigned int i=0; i<NSIG; i++){
        sigaction(i, &action, NULL);
    }
    signal(SIGPIPE, SIG_IGN);

    while(1) {
        int openPipes = 0;

        FOREACH_CHILD(&lib, it) {
            if(it->pipe_out >= 0) openPipes++;
            if(it->pipe_err >= 0) openPipes++;
        }

        struct pollfd fds[2 + openPipes];
        /* This FD signals when a child process died */
        const unsigned int SIGCHLD_FD = 0;
        fds[SIGCHLD_FD].fd = lib.chldFd[0];
        fds[SIGCHLD_FD].events = POLLIN;

        /* This FD is used to receive commands from the parent */
        const unsigned int CMD_FD = 1;
        fds[CMD_FD].fd = lib.socket;
        fds[CMD_FD].events = POLLIN;

        int numPoll = 2;

        FOREACH_CHILD(&lib, it) {
            if(it->pipe_out >= 0) {
                fds[numPoll].fd = it->pipe_out;
                fds[numPoll].events = POLLIN;
                numPoll++;
            }

            if(it->pipe_err >= 0) {
                fds[numPoll].fd = it->pipe_err;
                fds[numPoll].events = POLLIN;
                numPoll++;
            }
        }

        /* ppoll could be used as an alternative, but I find this code easier to follow */
        int retVal = poll(fds, numPoll, -1);
        if(retVal <= 0) {
            if(errno == EINTR) {
                continue;
            }
            slaveExit(&lib);
        }

        for(int i=2; i<numPoll; i++) {
            if(fds[i].revents) {
                if(fds[i].revents & POLLIN) {
                    char buffer[512];
                    ssize_t readLen = read(fds[i].fd, buffer, sizeof(buffer));
                    if(readLen > 0) {
                        FOREACH_CHILD(&lib, it) {
                            if(it->pipe_out == fds[i].fd || it->pipe_err == fds[i].fd) {
                                struct slaveResponse response;
                                if(it->pipe_err == fds[i].fd) {
                                    response.result = SLAVE_RESULT_CHILD_STDERR_DATA;
                                } else {
                                    response.result = SLAVE_RESULT_CHILD_STDOUT_DATA;
                                }

                                response.masterEcho = it->echo;

                                if(libChildWriteFull(NULL, fds[CMD_FD].fd, (char*)&response, sizeof(response))) {
                                    slaveExit(&lib);
                                }

                                if(libChildWriteVariable(NULL, fds[CMD_FD].fd, buffer, readLen)) {
                                    slaveExit(&lib);
                                }

                                break;
                            }
                        }
                    } else {
                        pipeClosed(fds[i].fd);
                    }
                }
                if(fds[i].revents & (POLLHUP | POLLERR)) {
                    pipeClosed(fds[i].fd);
                }
            }
        }

        if(fds[SIGCHLD_FD].revents & POLLIN) {
            siginfo_t sigInfo;
            if(recv(lib.chldFd[0], &sigInfo, sizeof(sigInfo), 0) != sizeof(sigInfo)) {
                slaveExit(&lib);
            }

            /* Is it SIGCHLD? */
            if(sigInfo.si_signo == SIGCHLD){
                int status;
                pid_t pid;

                while((pid = waitpid(-lib.grpId, &status, WNOHANG)) > 0) {
                    FOREACH_CHILD(&lib, it) {
                        if(it->pid == pid && it->running) {
                            it->status = status;
                            it->running = 0;

                            notifyDead(&lib, it);
                            break;
                        }
                    }
                }
            }else{
                struct slaveResponse response;
                response.result = SLAVE_RESULT_GOT_SIGNAL;
                if(libChildWriteFull(NULL, lib.socket, (char*)&response, sizeof(response))){
                    slaveExit(&lib);
                }
                if(libChildWriteFull(NULL, lib.socket, (char*)&sigInfo, sizeof(sigInfo))){
                    slaveExit(&lib);
                }
            }
        }

        if(fds[CMD_FD].revents & POLLIN) {
            struct slaveCommand cmd;
            if(libChildReadFull(fds[CMD_FD].fd, (char*)&cmd, sizeof(cmd), 0)) {
                slaveExit(&lib);
            }

            struct slaveResponse response;
            response.result = SLAVE_RESULT_NULL;
            response.masterEcho = cmd.masterEcho;

            if(cmd.command == SLAVE_COMMAND_EXEC || cmd.command == SLAVE_COMMAND_EXEC_PIPE) {
                int silent = (cmd.command == SLAVE_COMMAND_EXEC);

                /* Read parameters */
                char* program = libChildReadVariable(fds[CMD_FD].fd, NULL);
                if(!program) slaveExit(&lib);
                char* userName = libChildReadVariable(fds[CMD_FD].fd, NULL);
                if(!userName) slaveExit(&lib);
                char** argv = libChildReadPack(fds[CMD_FD].fd);
                if(!argv) slaveExit(&lib);
                char** env = libChildReadPack(fds[CMD_FD].fd);
                if(!env) slaveExit(&lib);

                response.result = SLAVE_RESULT_CHILD_CREATED;

                int pipe_stdout[2], pipe_stderr[2];
                if(!silent) {
                    if(pipe(pipe_stdout) || pipe(pipe_stderr)) {
                        slaveExit(&lib);
                    }
                }

                pid_t pid = fork();

                if(!pid) {
                    if(strlen(userName)) {
                        if(changeUser(userName) != 1) {
                            /* Don't exec anything unless we dropped privileges */
                            _exit (EXIT_FAILURE);
                        }
                    }

                    /* Close the command socket */
                    close(lib.socket);

                    /* Close all pipes except what we use */
                    if(!silent) {
                        close(pipe_stdout[0]);
                        close(pipe_stderr[0]);
                    }

                    /* Detach stdio */
                    detach(silent);

                    if(!silent) {
                        dup2(pipe_stdout[1], STDOUT_FILENO);
                        dup2(pipe_stderr[1], STDERR_FILENO);
                        close(pipe_stdout[1]);
                        close(pipe_stderr[1]);
                    }

                    /* Run */
                    execve(program, argv, env);
                    _exit (EXIT_FAILURE);

                } else if(pid < 0) {
                    response.paramChildProcess = NULL;

                    if(!silent) {
                        close(pipe_stdout[0]);
                        close(pipe_stderr[0]);
                    }

                } else {
                    struct childProcess* child = (struct childProcess*)malloc(sizeof(struct childProcess));
                    response.paramChildProcess = child;
                    response.paramInteger = pid;

                    child->running = 1;
                    child->pid = pid;
                    child->next = lib.firstProcess;
                    child->silent = silent;

                    if(silent) {
                        child->pipe_out = -1;
                        child->pipe_err = -1;
                    } else {
                        setCloExec(pipe_stdout[0]);
                        setCloExec(pipe_stderr[0]);
                        child->pipe_out = pipe_stdout[0];
                        child->pipe_err = pipe_stderr[0];
                    }

                    if(child->next) {
                        child->next->prev = child;
                    }

                    child->prev = NULL;
                    child->echo = cmd.masterEcho;

                    lib.firstProcess = child;
                }

                /* Close write part of the pipe */
                if(!silent) {
                    close(pipe_stdout[1]);
                    close(pipe_stderr[1]);
                }

                if(libChildWriteFull(NULL, fds[CMD_FD].fd, (char*)&response, sizeof(response))) {
                    slaveExit(&lib);
                }

                /* Free variable length things */
                free(program);
                libChildFreePack(argv);
                libChildFreePack(env);

            } else if (cmd.command == SLAVE_COMMAND_CLOSE_HANDLE) {
                struct childProcess* child = (struct childProcess*)cmd.paramChildProcess;
                if(child->prev) {
                    child->prev->next = child->next;
                } else {
                    lib.firstProcess = child->next;
                }
                if(child->next) {
                    child->next->prev = child->prev;
                }

                if(child->pipe_out >= 0) {
                    close(child->pipe_out);
                    child->pipe_out = -1;
                }
                if(child->pipe_err >= 0) {
                    close(child->pipe_err);
                    child->pipe_err = -1;
                }

                free(child);

            } else if (cmd.command == SLAVE_COMMAND_KILL) {
                struct childProcess* child = (struct childProcess*)cmd.paramChildProcess;
                if(child->running) {
                    kill(child->pid, cmd.paramInteger);
                }

            } else if (cmd.command == SLAVE_COMMAND_QUIT) {
                slaveExit(&lib);
            }
        }
    }
}
