/* exec.c - functions to exec a job
 *
 * (c) 2003-2015 Nicholas J. Kain <njkain at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include "nk/exec.h"
#include "nk/malloc.h"
#include "nk/xstrdup.h"
#include "nk/log.h"

#define DEFAULT_PATH "/bin:/usr/bin:/usr/local/bin"
#define MAX_ARGS 1024

void nk_fix_env(uid_t uid, bool chdir_home)
{
    if (clearenv())
        suicide("%s: clearenv failed: %s", __func__, strerror(errno));

    struct passwd *pw = getpwuid(uid);
    if (!pw)
        suicide("%s: user uid %u does not exist.  Not execing.",
                __func__, uid);

    char uids[20];
    ssize_t snlen = snprintf(uids, sizeof uids, "%i", uid);
    if (snlen < 0 || (size_t)snlen >= sizeof uids)
        suicide("%s: UID was truncated (%d).  Not execing.", __func__, snlen);
    if (setenv("UID", uids, 1))
        goto fail_fix_env;

    if (setenv("USER", pw->pw_name, 1))
        goto fail_fix_env;
    if (setenv("USERNAME", pw->pw_name, 1))
        goto fail_fix_env;
    if (setenv("LOGNAME", pw->pw_name, 1))
        goto fail_fix_env;

    if (setenv("HOME", pw->pw_dir, 1))
        goto fail_fix_env;
    if (setenv("PWD", pw->pw_dir, 1))
        goto fail_fix_env;

    if (chdir_home) {
        if (chdir(pw->pw_dir))
            suicide("%s: failed to chdir to uid %u's homedir.  Not execing.",
                    __func__, uid);
    } else {
        if (chdir("/"))
            suicide("%s: failed to chdir to root directory.  Not execing.",
                    __func__);
    }

    if (setenv("SHELL", pw->pw_shell, 1))
        goto fail_fix_env;
    if (setenv("PATH", DEFAULT_PATH, 1))
        goto fail_fix_env;
    return;
fail_fix_env:
    suicide("%s: failed to sanitize environment.  Not execing.", __func__);
}

void __attribute__((noreturn))
nk_execute(const char *command, const char *args)
{
    char *argv[MAX_ARGS];
    size_t curv = 0;

    if (!command)
        _Exit(EXIT_SUCCESS);

    // strip the path from the command name and set argv[0]
    const char *p = strrchr(command, '/');
    argv[curv] = xstrdup(p ? p + 1 : command);
    argv[++curv] = NULL;

    if (args) {
        p = args;
        const char *q = args;
        bool squote = false, dquote = false, atend = false;
        for (;; ++p) {
            switch (*p) {
            default: continue;
            case '\0':
                 atend = true;
                 goto endarg;
            case ' ':
                if (!squote && !dquote)
                    goto endarg;
                continue;
            case '\'':
                if (!dquote)
                    squote = !squote;
                continue;
            case '"':
                if (!squote)
                    dquote = !dquote;
                continue;
            }
endarg:
            {
                // Push an argument.
                size_t len = p - q + 1;
                if (len > 1) {
                    if (len > INT_MAX)
                        suicide("%s argument n=%zu length is too long", __func__, curv);
                    argv[curv] = xmalloc(len);
                    ssize_t snlen = snprintf(argv[curv], len, "%.*s", (int)(len - 1), q);
                    if (snlen < 0 || (size_t)snlen >= len)
                        suicide("%s: argument n=%zu would truncate.  Not execing.",
                                __func__, curv);
                    q = p + 1;
                    argv[++curv] = NULL;
                }
                if (atend || curv >= (MAX_ARGS - 1))
                    break;
            }
        }
    }
    execv(command, argv);
    log_error("%s: execv(%s) failed: %s", __func__, command, strerror(errno));
    _Exit(EXIT_FAILURE);
}

