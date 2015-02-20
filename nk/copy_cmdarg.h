#ifndef NCMLIB_COPY_CMDARG_H_
#define NCMLIB_COPY_CMDARG_H_

#include <stdio.h>
#include <stdlib.h>
#include "nk/log.h"

static inline void copy_cmdarg(char dest[static 1], const char src[static 1],
                               size_t destlen, const char argname[static 1])
{
    ssize_t olen = snprintf(dest, destlen, "%s", src);
    if (olen < 0)
        suicide("snprintf failed on %s; your system is broken?", argname);
    if ((size_t)olen >= destlen)
        suicide("snprintf would truncate %s arg; it's too long", argname);
}

#endif /* NCMLIB_COPY_CMDARG_H_ */
