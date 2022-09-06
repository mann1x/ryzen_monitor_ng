/**
 * Ryzen SMU Userspace Sensor Monitor and toolset
 *
 * Copyleft ManniX (github.com/mann1x)
 *
 * Based on work of:
 * Copyright (C) 2020-2021
 *    Florian Huehn <hattedsquirrel@gmail.com> (https://hattedsquirrel.net)
 *    Based on work of:
 *    Leonardo Gates <leogatesx9r@protonmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <cpuid.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>    
#include "commonfuncs.h"

void print_line(const char* label, const char* value_format, ...) {
    static char buffer[1024];
    va_list list;

    va_start(list, value_format);
    vsnprintf(buffer, sizeof(buffer), value_format, list);
    va_end(list);

    fprintf(stdout, "│ %45s │ %46s │\n", label, buffer);
   
}

void append_u32_to_str(char* buffer, unsigned int val) {
    buffer[0] = val & 0xff;
    buffer[1] = (val >>  8) & 0xff;
    buffer[2] = (val >> 16) & 0xff;
    buffer[3] = (val >> 24) & 0xff;
}


/* msleep(): Sleep for the requested number of milliseconds. */
int msleep(long msec)
{
    struct timespec ts;
    int res;

    if (msec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

unsigned int count_set_bits(unsigned int v) {
    unsigned int result = 0;

    while(v!=0) {
        if(v&1) result++;
        v >>= 1;
    }

    return result;
}

