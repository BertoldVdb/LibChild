#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <grp.h>

int changeUser(char* username)
{
    if (getuid() != 0) {
        return -1;
    }

    struct passwd pwd, *result;
    int buflen = sysconf(_SC_GETPW_R_SIZE_MAX), retVal;
    char* buf;

    /* Try to get user information */
    for(;;) {
        buf = malloc(buflen);
        if(!buf) return -1;
        retVal = getpwnam_r(username, &pwd, buf, buflen, &result);
        printf ("%d %u %p\n", retVal, buflen, result);
        if(retVal == ERANGE) {
            free(buf);
            buflen *= 2;
            continue;
        } else {
            break;
        }
    }

    if(!result){
        free(buf);
        return 0;
    }

    uid_t uid = result->pw_uid;
    uid_t gid = result->pw_gid;
    free(buf);

    if(setgid(gid) != 0) return -1;
    if(initgroups(username, gid)) return -1;
    if(setuid(uid) != 0) return -1;

    return 1;
}
