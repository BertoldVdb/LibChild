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
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "def.h"


struct childProcess{
	struct childProcess* next;
	struct childProcess* prev;

	int		running;
	pid_t	pid;

	void*	echo;

	int		pipe_out;
	int 	pipe_err;
	int 	silent;
};

typedef struct {
	pid_t intermediatePid;
	pid_t grpId;
	int chldFd;
	int socket;
	int die;
	struct childProcess* firstProcess;
} SlaveGlobal;

static void slaveExit(SlaveGlobal* lib){
	struct childProcess* it = lib->firstProcess;
	while(it){
		if(it->pipe_out >= 0) {
			close(it->pipe_out);
		}
		if(it->pipe_err >= 0) {
			close(it->pipe_err);
		}

		if(it->running){
			kill(it->pid, SIGKILL);

			/* Try to write something to the master, it may still be listening... */
			struct slaveResponse response;
			response.result = SLAVE_RESULT_CHILD_DIED;
			response.paramChildProcess = it;
			response.masterEcho = it->echo;

			libChildWriteFull(lib->socket, (char*)&response, sizeof(response));
		}
		struct childProcess* next = it->next;
		free(it);
		it = next;
	}

	close(lib->socket);
	_exit (EXIT_FAILURE);
}

static void detach(int silent){
    umask(0);
    int retVal = chdir("/");
    (void)retVal;

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

	int fd = open("/dev/null", O_RDONLY);
	dup2(fd, STDIN_FILENO);
	close(fd);

    if(silent){
    	fd = open("/dev/null", O_RDWR);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
    }
}

static void notifyDead(SlaveGlobal* lib, struct childProcess* it){
	if(it->running) return;
	if(!it->silent){
		if(it->pipe_out >= 0) return;
		if(it->pipe_err >= 0) return;
	}

	struct slaveResponse response;
	response.result = SLAVE_RESULT_CHILD_DIED;
	response.paramChildProcess = it;
	response.masterEcho = it->echo;

	if(libChildWriteFull(lib->socket, (char*)&response, sizeof(response))){
		slaveExit(lib);
	}
}

#define FOREACH_CHILD(lib,it)   for(struct childProcess* it = (lib)->firstProcess; it; it = it->next)

SlaveGlobal lib;

static void signalHandler(int sig, siginfo_t *siginfo, void *context){
	uint64_t value = 1;
	if(sig == SIGTERM){
		lib.die = 1;
	}

	int retVal = write(lib.chldFd, &value, sizeof(value));
	(void)retVal;
}

void libChildSlaveProcess(int socket){
	/* Disconnect standard IO */
	detach(1);

	lib.firstProcess = NULL;
	lib.socket = socket;
	lib.die = 0;

	/* Become a session leader and create new process group */
	lib.grpId = setsid();
	if(lib.grpId < 0){
		slaveExit(&lib);
	}

	/* Create an eventID to synchronize the SIGCHLD signal */
	lib.chldFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if(lib.chldFd < 0){
		slaveExit(&lib);
	}

	/* We have forked already once so we can safely put signal handlers */
	struct sigaction action;
	action.sa_sigaction = &signalHandler;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);
	signal(SIGPIPE, SIG_IGN);

	while(1){
		int openPipes = 0;

		FOREACH_CHILD(&lib, it){
			if(it->pipe_out >= 0) openPipes++;
			if(it->pipe_err >= 0) openPipes++;
		}

		struct pollfd fds[2 + openPipes];
		/* This FD signals when a child process died */
		const unsigned int SIGCHLD_FD = 0;
		fds[SIGCHLD_FD].fd = lib.chldFd;
		fds[SIGCHLD_FD].events = POLLIN;

		/* This FD is used to receive commands from the parent */
		const unsigned int CMD_FD = 1;
		fds[CMD_FD].fd = lib.socket;
		fds[CMD_FD].events = POLLIN;

		int numPoll = 2;

		FOREACH_CHILD(&lib, it){
			if(it->pipe_out >= 0){
				fds[numPoll].fd = it->pipe_out;
				fds[numPoll].events = POLLIN;
				numPoll++;
			}

			if(it->pipe_err >= 0){
				fds[numPoll].fd = it->pipe_err;
				fds[numPoll].events = POLLIN;
				numPoll++;
			}
		}

		/* ppoll could be used as an alternative, but I find this code easier to follow */
		int retVal = poll(fds, numPoll, -1);
		if(retVal <= 0){
			if(errno == EINTR){
				continue;
			}
			slaveExit(&lib);
		}

		if(lib.die){
			slaveExit(&lib);
		}

		for(int i=2; i<numPoll; i++){
			if(fds[i].revents){
				if(fds[i].revents & POLLIN){
					char buffer[512];
					ssize_t readLen = read(fds[i].fd, buffer, sizeof(buffer));

					if(readLen > 0){
						FOREACH_CHILD(&lib, it){
							if(it->pipe_out == fds[i].fd || it->pipe_err == fds[i].fd){
								struct slaveResponse response;
								if(it->pipe_err == fds[i].fd){
									response.result = SLAVE_RESULT_CHILD_STDERR_DATA;
								}else{
									response.result = SLAVE_RESULT_CHILD_STDOUT_DATA;
								}

								response.masterEcho = it->echo;

								if(libChildWriteFull(fds[CMD_FD].fd, (char*)&response, sizeof(response))){
									slaveExit(&lib);
								}

								if(libChildWriteVariable(fds[CMD_FD].fd, buffer, readLen)){
									slaveExit(&lib);
								}

								break;
							}
						}
					}
				}else if(fds[i].revents & (POLLHUP | POLLERR)){
					FOREACH_CHILD(&lib, it){
						if(it->pipe_out == fds[i].fd){
							close(it->pipe_out);
							it->pipe_out = -1;
							notifyDead(&lib, it);
							break;
						}
						if(it->pipe_err == fds[i].fd){
							close(it->pipe_err);
							it->pipe_err = -1;
							notifyDead(&lib, it);
							break;
						}
					}
				}
			}
		}

		if(fds[SIGCHLD_FD].revents & POLLIN){
			uint64_t numDead;
			if(read(lib.chldFd, &numDead, sizeof(numDead)) != sizeof(numDead)){
				slaveExit(&lib);
			}

			for(uint64_t i=0; i<numDead; i++){
				/* Wait for children */
				int status;
				pid_t pid = waitpid(-lib.grpId, &status, 0);

				FOREACH_CHILD(&lib, it){
					if(it->pid == pid && it->running){
						it->running = 0;

						notifyDead(&lib, it);
						break;
					}
				}
			}
		}

		if(fds[CMD_FD].revents & POLLIN){
			struct slaveCommand cmd;
			if(libChildReadFull(fds[CMD_FD].fd, (char*)&cmd, sizeof(cmd))){
				slaveExit(&lib);
			}

			struct slaveResponse response;
			response.result = SLAVE_RESULT_NULL;
			response.masterEcho = cmd.masterEcho;

			if(cmd.command == SLAVE_COMMAND_EXEC || cmd.command == SLAVE_COMMAND_EXEC_PIPE){
				int silent = (cmd.command == SLAVE_COMMAND_EXEC);

				/* Read parameters */
				char* program = libChildReadVariable(fds[CMD_FD].fd, NULL);
				if(!program) slaveExit(&lib);
				char** argv = libChildReadPack(fds[CMD_FD].fd);
				if(!argv) slaveExit(&lib);
				char** env = libChildReadPack(fds[CMD_FD].fd);
				if(!env) slaveExit(&lib);

				response.result = SLAVE_RESULT_CHILD_CREATED;

				int pipe_stdout[2], pipe_stderr[2];
				if(!silent){
					if(pipe(pipe_stdout) || pipe(pipe_stderr)){
						slaveExit(&lib);
					}
				}

				pid_t pid = fork();

				if(!pid){
					/* Close the command socket */
					close(lib.socket);

					/* Close all pipes except what we use */
					if(!silent){
						close(pipe_stdout[0]);
						close(pipe_stderr[0]);
					}
					FOREACH_CHILD(&lib, it){
						if(it->pipe_out >= 0) close(it->pipe_out);
						if(it->pipe_err >= 0) close(it->pipe_err);
					}

					/* Detach stdio */
					detach(silent);

					if(!silent){
						dup2(pipe_stdout[1], STDOUT_FILENO);
						dup2(pipe_stderr[1], STDERR_FILENO);
						close(pipe_stdout[1]);
						close(pipe_stderr[1]);
					}

					/* Run */
					execve(program, argv, env);
					_exit (EXIT_FAILURE);

				}else if(pid < 0){
					response.paramChildProcess = NULL;

					if(!silent){
						close(pipe_stdout[0]);
						close(pipe_stderr[0]);
					}

				}else{
					struct childProcess* child = (struct childProcess*)malloc(sizeof(struct childProcess));
					response.paramChildProcess = child;
					response.paramInteger = pid;

					child->running = 1;
					child->pid = pid;
					child->next = lib.firstProcess;
					child->silent = silent;

					if(silent){
						child->pipe_out = -1;
						child->pipe_err = -1;
					}else{
						child->pipe_out = pipe_stdout[0];
						child->pipe_err = pipe_stderr[0];
					}

					if(child->next){
						child->next->prev = child;
					}

					child->prev = NULL;
					child->echo = cmd.masterEcho;

					lib.firstProcess = child;
				}

				/* Close write part of the pipe */
				if(!silent){
					close(pipe_stdout[1]);
					close(pipe_stderr[1]);
				}

				if(libChildWriteFull(fds[CMD_FD].fd, (char*)&response, sizeof(response))){
					slaveExit(&lib);
				}

				/* Free variable length things */
				free(program);
				libChildFreePack(argv);
				libChildFreePack(env);

			}else if (cmd.command == SLAVE_COMMAND_CLOSE_HANDLE){
				struct childProcess* child = (struct childProcess*)cmd.paramChildProcess;
				if(child->prev){
					child->prev->next = child->next;
				}else{
					lib.firstProcess = child->next;
				}
				if(child->next){
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

			}else if (cmd.command == SLAVE_COMMAND_KILL){
				struct childProcess* child = (struct childProcess*)cmd.paramChildProcess;
				if(child->running){
					kill(child->pid, cmd.paramInteger);
				}

			}else if (cmd.command == SLAVE_COMMAND_QUIT){
				slaveExit(&lib);
			}
		}
	}
}