#ifndef MPD_CUE_TAG_H
#define MPD_CUE_TAG_H

#include "check.h"

#ifdef HAVE_CUE /* libcue */

#include <stdio.h>

struct tag;

struct tag *
cue_tag_file(FILE *file, unsigned tnum);

struct tag *
cue_tag_string(char *str, unsigned tnum);

#endif /* libcue */
#endif
