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
#include <libsmu.h>
#include <time.h>
#include <errno.h>    
#include "readinfo.h"
#include "commonfuncs.h"

extern smu_obj_t obj;

#define READ_SMN_V1(offs) { if (smu_read_smn_addr(&obj, offs + offset, &value1) != SMU_Return_OK) goto _READ_ERROR; }
#define READ_SMN_V2(offs) { if (smu_read_smn_addr(&obj, offs + offset, &value2) != SMU_Return_OK) goto _READ_ERROR; }
#define SEND_CMD_RSMU(op) { if (smu_send_command(&obj, op, &args, TYPE_RSMU) != SMU_Return_OK) goto _SEND_ERROR; }
#define SEND_CMD_MP1(op) { if (smu_send_command(&obj, op, &args, TYPE_MP1) != SMU_Return_OK) goto _SEND_ERROR; }

const char* get_processor_name() {
    unsigned int eax, ebx, ecx, edx;
    int i;
    static char buffer[50] = { 0 }, *p;

    i=0;
    __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer+i, eax); i+=4;
    append_u32_to_str(buffer+i, ebx); i+=4;
    append_u32_to_str(buffer+i, ecx); i+=4;
    append_u32_to_str(buffer+i, edx); i+=4;

    __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer+i, eax); i+=4;
    append_u32_to_str(buffer+i, ebx); i+=4;
    append_u32_to_str(buffer+i, ecx); i+=4;
    append_u32_to_str(buffer+i, edx); i+=4;

    __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer+i, eax); i+=4;
    append_u32_to_str(buffer+i, ebx); i+=4;
    append_u32_to_str(buffer+i, ecx); i+=4;
    append_u32_to_str(buffer+i, edx); i+=4;

    // Trim whitespaces
    p = buffer;
    i = strlen(p)-1;
    while(isspace(p[i]) && i>=0) p[i--]=0;
    while(*p && isspace(*p)) p++;

    return p;
}

void get_processor_topology(system_info *sysinfo, int debug_init) {
    unsigned int ccds_present, ccds_down, ccd_enable_map, ccd_disable_map, ccx_per_ccd, ccd_offset = 0,
        core_disable_map_addr, core_disable_map_tmp, logical_cores, threads_per_core, physical_cores,
        fam, model, fuse1, fuse2, offs, eax, ebx, ecx, edx;

    __get_cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
    fam = ((eax & 0xf00) >> 8) + ((eax & 0xff00000) >> 20);
    sysinfo->family = fam;
    model = ((eax & 0xf0000) >> 12) + ((eax & 0xf0) >> 4);
    sysinfo->model = model;
    logical_cores = (ebx >> 16) & 0xFF;

    __get_cpuid(0x8000001E, &eax, &ebx, &ecx, &edx);
    threads_per_core = ((ebx >> 8) & 0xF) + 1;

    fuse1 = 0x5D218;
    fuse2 = 0x5D21C;
    offs = 0x238;
    ccx_per_ccd = 2;

    if (fam == 0x19) {
        //fuse1 += 0x10;
        //fuse2 += 0x10;
        offs = 0x598;
        ccx_per_ccd = 1;
    }
    else if (fam == 0x17 && model != 0x71 && model != 0x31) {
        fuse1 += 0x40;
        fuse2 += 0x40;
    }

    if (smu_read_smn_addr(&obj, fuse1, &ccds_present) != SMU_Return_OK ||
        smu_read_smn_addr(&obj, fuse2, &ccds_down) != SMU_Return_OK) {
        perror("Failed to read CCD fuses");
        exit(-1);
    }

    ccd_enable_map = (ccds_present >> 22) & 0xff;
    ccd_disable_map = ((ccds_down & 0x3f) << 2) | ((ccds_present >> 30) & 0x3);

    core_disable_map_addr = (fam == 0x19 && model == 0x50) ? 0x5D448 : (0x30081800 + offs);

    ccd_enable_map = count_set_bits(ccd_enable_map) == 0 ? 0x1 : ccd_enable_map;
    //sysinfo->ccds = count_set_bits(ccd_enable_map) > 0 ? count_set_bits(ccd_enable_map) : 1;
    sysinfo->ccds = count_set_bits(ccd_enable_map);
    sysinfo->ccxs = sysinfo->ccds * ccx_per_ccd;
    sysinfo->physical_cores = (sysinfo->ccxs * 8) / ccx_per_ccd;

    if (smu_read_smn_addr(&obj, core_disable_map_addr, &core_disable_map_tmp) != SMU_Return_OK) {
        perror("Failed to read disabled core fuse");
        exit(-1);
    }

    if (fam == 0x19 && model == 0x50) {
        sysinfo->core_disable_map = (core_disable_map_tmp >> 11) & 0xFF;
    } else {
        for (unsigned int i = 0; i < sysinfo->ccds; i++)
        {
            if (ccd_enable_map & i)
            {
                if (smu_read_smn_addr(&obj, core_disable_map_addr | ccd_offset, &core_disable_map_tmp) != SMU_Return_OK) {
                    perror("Failed to read disabled core fuse for CCD");
                    exit(-1);
                }
                sysinfo->core_disable_map |= (core_disable_map_tmp & 0xff) << i * 8;
            }
            ccd_offset += 0x2000000;
        }
    }

    sysinfo->cores_per_ccx = 8 - count_set_bits(sysinfo->core_disable_map & 0xff) / ccx_per_ccd;

    if (!threads_per_core)
        sysinfo->cores = logical_cores;
    else
        sysinfo->cores = logical_cores / threads_per_core;

    /*
    if (sysinfo->family == 0x17 && sysinfo->model == 0x18) {
        sysinfo->core_disable_map = sysinfo->core_disable_map_pmt;
    }
    */

    sysinfo->enabled_cores_count = sysinfo->physical_cores-count_set_bits(sysinfo->core_disable_map);

    sysinfo->coremap=(int *)malloc(sysinfo->cores * sizeof(int));
    
    int c, cx = 0, core_disabled;
    
    for (c = 0; c < 8*(sysinfo->ccds); c++) {
        core_disabled = (sysinfo->core_disable_map >> c)&0x01;
        if (cx >= sysinfo->cores) break;
        if (!core_disabled) {
            sysinfo->coremap[cx] = c;
            cx++;
        }
    } 

    if (debug_init) {
        fprintf(stdout, "\nFamily: 0x%X Model: 0x%X\n", sysinfo->family, sysinfo->model);
        fprintf(stdout, "ccds: %i ccxs: %i cores: %i cores_per_ccx: %i enabled: %i\n", sysinfo->ccds, sysinfo->ccxs, sysinfo->cores, sysinfo->cores_per_ccx, sysinfo->enabled_cores_count);
        fprintf(stdout, "core_disable_map: 0x%X tmp: 0x%X pmt: 0x%X core_disable_addr: 0x%X\n", sysinfo->core_disable_map, core_disable_map_tmp, sysinfo->core_disable_map_pmt, core_disable_map_addr);
        fprintf(stdout, "ccds_present: 0x%X  ccds_down: 0x%X\n", ccds_present, ccds_down);
        fprintf(stdout, "ccd_enable_map: 0x%X ccd_disable_map: 0x%X\n", ccd_enable_map, ccd_disable_map);
        fprintf(stdout, "\n");
    }

    sysinfo->available=1;
}

void print_memory_timings() {
    const char* bool_str[2] = { "Disabled", "Enabled" };
    unsigned int value1, value2, offset;

    READ_SMN_V1(0x50200);
    offset = value1 == 0x300 ? 0x100000 : 0;

    READ_SMN_V1(0x50050); READ_SMN_V2(0x50058);
    fprintf(stdout, "BankGroupSwap: %s\n",
        bool_str[!(value1 == value2 && value1 == 0x87654321)]);

    READ_SMN_V1(0x500D0); READ_SMN_V2(0x500D4);
    fprintf(stdout, "BankGroupSwapAlt: %s\n",
        bool_str[(value1 >> 4 & 0x7F) != 0 || (value2 >> 4 & 0x7F) != 0]);

    READ_SMN_V1(0x50200); READ_SMN_V2(0x50204);
    fprintf(stdout, "Memory Clock: %.0f MHz\nGDM: %s\nCR: %s\nTcl: %d\nTras: %d\nTrcdrd: %d\nTrcdwr: %d\n",
        (value1 & 0x7f) / 3.f * 100.f,
        bool_str[((value1 >> 11) & 1) == 1],
        ((value1 & 0x400) >> 10) != 0 ? "2T" : "1T",
        value2 & 0x3f,
        value2 >> 8 & 0x7f,
        value2 >> 16 & 0x3f,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50208); READ_SMN_V2(0x5020C);
    fprintf(stdout, "Trc: %d\nTrp: %d\nTrrds: %d\nTrrdl: %d\nTrtp: %d\n",
        value1 & 0xff,
        value1 >> 16 & 0x3f,
        value2 & 0x1f,
        value2 >> 8 & 0x1f,
        value2 >> 24 & 0x1f);

    READ_SMN_V1(0x50210); READ_SMN_V2(0x50214);
    fprintf(stdout, "Tfaw: %d\nTcwl: %d\nTwtrs: %d\nTwtrl: %d\n",
        value1 & 0xff,
        value2 & 0x3f,
        value2 >> 8 & 0x1f,
        value2 >> 16 & 0x3f);

    READ_SMN_V1(0x50218); READ_SMN_V2(0x50220);
    fprintf(stdout, "Twr: %d\nTrdrddd: %d\nTrdrdsd: %d\nTrdrdsc: %d\nTrdrdscl: %d\n",
        value1 & 0xff,
        value2 & 0xf,
        value2 >> 8 & 0xf,
        value2 >> 16 & 0xf,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50224); READ_SMN_V2(0x50228);
    fprintf(stdout, "Twrwrdd: %d\nTwrwrsd: %d\nTwrwrsc: %d\nTwrwrscl: %d\nTwrrd: %d\nTrdwr: %d\n",
        value1 & 0xf,
        value1 >> 8 & 0xf,
        value1 >> 16 & 0xf,
        value1 >> 24 & 0x3f,
        value2 & 0xf,
        value2 >> 8 & 0x1f);

    READ_SMN_V1(0x50254);
    fprintf(stdout, "Tcke: %d\n", value1 >> 24 & 0x1f);

    READ_SMN_V1(0x50260); READ_SMN_V2(0x50264);
    if (value1 != value2 && value1 == 0x21060138)
        value1 = value2;

    fprintf(stdout, "Trfc: %d\nTrfc2: %d\nTrfc4: %d\n",
        value1 & 0x3ff,
        value1 >> 11 & 0x3ff,
        value1 >> 22 & 0x3ff);

    exit(0);

_READ_ERROR:
    fprintf(stderr, "Unable to read SMN address space.\n");
    exit(1);
}

int select_pm_table_version(unsigned int version, pm_table *pmt, unsigned char *pm_buf) {
    //Initialize pmt to 0. This also sets all pointers to 0, which signifies non-existiting fields.
    //Access via pmta(...) will check if pointer is 0 before trying to access the value.
    memset(pmt, 0, sizeof(pm_table));

     //Select matching PM Table
    switch(version) {
        case 0x380904: pm_table_0x380904(pmt, pm_buf); break; //Ryzen 5600X
        case 0x380905: pm_table_0x380905(pmt, pm_buf); break; //Ryzen 5600X
        case 0x380804: pm_table_0x380804(pmt, pm_buf); break; //Ryzen 5900X / 5950X
        case 0x380805: pm_table_0x380805(pmt, pm_buf); break; //Ryzen 5900X / 5950X
        case 0x400005: pm_table_0x400005(pmt, pm_buf); break; //Ryzen 5600G / 5700G
        case 0x240903: pm_table_0x240903(pmt, pm_buf); break; //Ryzen 3700X / 3800X
        case 0x240803: pm_table_0x240803(pmt, pm_buf); break; //Ryzen 3950X
        case 0x370003: pm_table_0x370003(pmt, pm_buf); break; //Lucienne, Renoir
        case 0x370005: pm_table_0x370005(pmt, pm_buf); break; //Lucienne, Renoir
        case 0x1E0004: pm_table_0x1E0004(pmt, pm_buf); break; //Picasso
        
        default:
            return 0;
    }

    //Avoid access bejond bounds of the defined arrays.
    if (pmt->max_l3 > PMT_MAX_NUM_L3) pmt->max_l3 = PMT_MAX_NUM_L3;
    if (pmt->max_cores > PMT_MAX_NUM_CORES) pmt->max_cores = PMT_MAX_NUM_CORES;

    //APML_POWER is probably identical to PACKAGE_POWER
    if (pmt->PACKAGE_POWER == NULL) pmt->PACKAGE_POWER = pmt->APML_POWER;

    if (pmt->VDD18_POWER == NULL) pmt->VDD18_POWER = pmt->IO_VDD18_POWER;

    return 1;
}