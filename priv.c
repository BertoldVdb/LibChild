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
