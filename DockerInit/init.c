#include "init.h"
#include "libchild.h"
#include <stdio.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

LibChild* lib;
int taskShutdown = 0;

struct task {
    Child* child;
    char* program;
    char* username;
    char** argv;
    char** env;
    int restart;
    int taskId;
};

struct task* activeTasks[128] = {};

static void stateChange(Child* child, void* param, enum childStates state);

static void childData(Child* child, void* param, char* buffer, size_t len, int isErr){
    printf("Child=%p, isErr=%u, Buffer=\"%s\"\n", child, isErr, buffer);
}

static void runTask(struct task* t){
    t->child = libChildExec(lib, t->program, t->username, t->argv, t->env, stateChange, childData, t);
}

static void stateChange(Child* child, void* param, enum childStates state){
    struct task* t = (struct task*)param;
    
    if(state == 2){
        libChildFreeHandle(child);
        sleep(1);

        if(t->restart && !taskShutdown){
            runTask(t);
        }else{
            activeTasks[t->taskId] = NULL;
            free(t);
        }
    }
}

static void signalReceived(siginfo_t sig, void* param){
    if(sig.si_signo == SIGTERM){
        taskShutdown = 1;
    }
}

int newTask(char* program, char* username, char** argv, char** env, int restart){
    struct task* t = (struct task*)malloc(sizeof(struct task));
    if(!t){
        return -1;
    }
    
    memset(t, 0, sizeof(*t));

    t->taskId=-1;

    for(int i=0; i<sizeof(activeTasks)/sizeof(struct task*); i++){
        if(activeTasks[i] == NULL){
            activeTasks[i] = t;
            t->taskId = i;
            break;
        }
    }

    if(t->taskId < 0){
        goto failed;
    }

    t->program = program;
    t->username = username;
    t->argv = argv;
    t->env = env;
    t->restart = restart;

    runTask(t);

    return 0;

failed:
    free(t);
    return -1;
}

int main(int argc, char** argv){
    if(getpid() == 1){
        lib = libChildInPlace(signalReceived, NULL);
    }else{
        lib = libChildCreateWorker("agent", NULL, signalReceived, NULL);
    }

    if(!lib){
        return -1;
    }

    char* emptyPack[] = {NULL};
    newTask("/tmp/a.sh", NULL, emptyPack, __environ, 1); 

    const unsigned int LIBCHILDFD = 0;

    while(!taskShutdown){
        struct pollfd fds[1];
        fds[LIBCHILDFD].fd = libChildGetFd(lib);
        fds[LIBCHILDFD].events = POLLIN; 

        int retVal = poll(fds, 1, 60000);
        if(retVal < 0){
            if(errno == EINTR){
                continue;
            }
            break;
        }

        if(retVal > 0){
            if(fds[LIBCHILDFD].revents & POLLIN){
                if(libChildPoll(lib)){
                    break;
                }
            }
        }
    }

    printf("Sending SIGTERM to all active tasks\n");
    for(int i=0; i<sizeof(activeTasks)/sizeof(struct task*); i++){
        if(activeTasks[i] != NULL){
            libChildKill(activeTasks[i]->child, SIGTERM);
        }
    }
    sleep(5);
    libChildPoll(lib);

    printf("Sending SIGKILL to all remaining tasks\n");
    for(int i=0; i<sizeof(activeTasks)/sizeof(struct task*); i++){
        if(activeTasks[i] != NULL){
            libChildKill(activeTasks[i]->child, SIGKILL);
        }
    }
    sleep(2);
    libChildPoll(lib);

    libChildTerminateWorker(lib);
    return 0;
}
