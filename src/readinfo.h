/**
 * Ryzen SMU Userspace Sensor Monitor and toolset
 *
 * Copyleft ManniX (github.com/mann1x)
 *
 * Based on work of:
 * Copyright (C) 2020-2021
 *    Florian Huehn <hattedsquirrel@gmail.com> (https://hattedsquirrel.net)
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

#ifndef READINFO_H
#define READINFO_H

#include "pm_tables.h"

typedef struct {
    char available;
    const char *cpu_name;
    const char *codename;
    const char *smu_fw_ver;
    int smu_codename;   
    unsigned int if_ver;   
    unsigned int cores;
    unsigned int ccds;
    unsigned int ccxs;
    unsigned int cores_per_ccx;
    unsigned int core_disable_map;
    unsigned int core_disable_map_pmt;
    unsigned int enabled_cores_count;
    unsigned int physical_cores;
    unsigned int family;
    unsigned int model;
    int *coremap;
} system_info;

void print_memory_timings();
void get_processor_topology(system_info *sysinfo, int init_debug);
unsigned int count_set_bits(unsigned int v);
const char* get_processor_name();
void append_u32_to_str(char* buffer, unsigned int val);
int select_pm_table_version(unsigned int version, pm_table *pmt, unsigned char *pm_buf);

#endif
