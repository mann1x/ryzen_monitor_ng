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

#define _GNU_SOURCE

#include <errno.h>
#include <math.h>
#include <sched.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <inttypes.h>

#include <termios.h>

#include <libsmu.h>
#include "readinfo.h"
#include "setinfo.h"
#include "pm_tables.h"
#include "commonfuncs.h"
#include "argparse.h"

#define PROGRAM_VERSION "2.0.3"
#define BUF_SIZE 65536
#define getName(var) #var

#define pm_element(i) ((float*) (dumpbuf + (i)*4))

smu_obj_t obj;
pm_table pmt;
system_info sysinfo;


static int update_time_s = 1;
static int export_update_time_s = 10;
int show_disabled_cores = 0;
static int cmd_mode = 0;
int debuglog = 1;

int fdpipe = 0;
char *pm_export_pipe = 0;

int view_compact = 0, view_info = 1, view_counts = 1, view_electrical = 1, view_memory = 1, view_gfx = 1, view_power = 1;

//Helper to access the PM Table elements. If an element doesn't exist in the
//current PM Table version, it's pointer is set to 0. This helper returns
//NAN for not available fields.
#define pmta(elem) ((pmt->elem)?(*pmt->elem):NAN)
//Same, but with 0 as return. For summations that should not fail if one value is not present.
#define pmta0(elem) ((pmt->elem)?(*pmt->elem):0)

#define for_each_item(item, list) \
    for(T * item = list->head; item != NULL; item = item->next)

void draw_screen(pm_table *pmt, system_info *sysinfo) {
    //general
    int i, j, k, l;
    //core block
    float core_voltage, core_frequency, package_sleep_time, core_sleep_time, average_voltage;
    float peak_core_frequency, peak_core_temp, peak_core_voltage, smu_peak_core_voltage;
    float total_core_voltage, total_core_power, total_usage, total_core_CC6;
    float thm_value = 0;

    int core_disabled, core_number;
    //constraints block
    float edc_value, ppt_limit_apu;
    //power block
    float l3_logic_power, l3_vddm_power;
    char strbuf[100];

    if (pmt->experimental) {
        fprintf(stdout, "Warning: Support for this PM table version is experimental. Can't trust anything.\n");
    }

    if (sysinfo->available && view_info && !view_compact) {
        fprintf(stdout, "╭───────────────────────────────────────────────┬────────────────────────────────────────────────╮\n");
        print_line("CPU Model", sysinfo->cpu_name);
        print_line("Processor Code Name", sysinfo->codename);
        print_line("Cores", "%d", sysinfo->cores);
        print_line("Core CCDs", "%d", sysinfo->ccds);
        if (pmt->zen_version!=3) {
            print_line("Core CCXs", "%d", sysinfo->ccxs);
            print_line("Cores Per CCX", "%d", sysinfo->cores_per_ccx);
        }
        else
            print_line("Cores Per CCD", "%d", sysinfo->cores_per_ccx); //Zen3 does not have CCXs anymore
        print_line("SMU FW Version", "v%s", sysinfo->smu_fw_ver);
        print_line("MP1 IF Version", "v%d", sysinfo->if_ver);
        fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");
    }

    peak_core_frequency = peak_core_temp = peak_core_voltage = 0;
    total_core_voltage = total_core_power = total_usage = total_core_CC6 = 0;
    core_number = 0;

    for (i = 0; i < pmt->max_cores; i++) {
        core_disabled = (sysinfo->core_disable_map >> i)&0x01;
        average_voltage = i > 0 ? (average_voltage+pmta(CORE_VOLTAGE[i]))/2 : pmta(CORE_VOLTAGE[i]);
        if (!core_disabled) core_number++;
    }

    core_number = 0;

    if (!average_voltage > 0)
        average_voltage = pmta(CPU_TELEMETRY_VOLTAGE);

    if(pmt->PC6)
    {
        package_sleep_time = pmta(PC6) / 100.f;
        average_voltage = ((average_voltage) - (0.2 * package_sleep_time)) / (1.0 - package_sleep_time);
    }

    fprintf(stdout, "╭─────────┬────────────┬──────────┬─────────┬──────────┬─────────────┬─────────────┬─────────────╮\n");
    for (i = 0; i < pmt->max_cores; i++) {
        core_disabled = (sysinfo->core_disable_map >> i)&0x01;
        core_frequency = pmta(CORE_FREQEFF[i]) * 1000.f;

        if (!pmt->THM_VALUE) {
            if (pmta0(THM_VALUE_CORES[i]) > 0 && pmta0(THM_VALUE_CORES[i]) > thm_value)
                thm_value = pmta0(THM_VALUE_CORES[i]);
        }

        core_voltage = pmta(CORE_VOLTAGE[i]); // True core voltage
        // Rumours say this is how AMD calculates core voltage
        //if (pmta(CORE_FREQ[i]) != 0.f) {
        core_sleep_time = pmta0(CORE_CC6[i]) / 100.f;
        if (core_sleep_time > 0 && average_voltage < 2)
            core_voltage = ((1.0 - core_sleep_time) * average_voltage) + (0.2 * core_sleep_time);
        //}

        if (core_disabled) {
            if (show_disabled_cores)
                    fprintf(stdout,
                        "│ %*s %d │   Disabled | %6.3f W | %5.3f V | %6.2f C | C0: %5.1f %% | C1: %5.1f %% | C6: %5.1f %% │\n",
                    (core_number<10)+4, "Core", core_number, //Print "Core" and its number but right-justified
                        pmta(CORE_POWER[i]), core_voltage, pmta(CORE_TEMP[i]),
                        pmta(CORE_C0[i]), pmta(CORE_CC1[i]), pmta(CORE_CC6[i]));
        }
        else if (pmta(CORE_C0[i]) >= 6.f) {
            // AMD denotes a sleeping core as having spent less than 6% of the time in C0.
            // Source: Ryzen Master
                fprintf(stdout,
                    "│ %*s %d │   %4.f MHz | %6.3f W | %5.3f V | %6.2f C | C0: %5.1f %% | C1: %5.1f %% | C6: %5.1f %% │\n",
                (core_number<10)+4, "Core", core_number, //Print "Core" and its number but right-justified
                core_frequency, pmta(CORE_POWER[i]), core_voltage, pmta(CORE_TEMP[i]),
                    pmta(CORE_C0[i]), pmta(CORE_CC1[i]), pmta(CORE_CC6[i]));
            }
            else {
                fprintf(stdout,
                    "│ %*s %d │   Sleeping | %6.3f W | %5.3f V | %6.2f C | C0: %5.1f %% | C1: %5.1f %% | C6: %5.1f %% │\n",
                (core_number<10)+4, "Core", core_number, //Print "Core" and its number but right-justified
                    pmta(CORE_POWER[i]), core_voltage, pmta(CORE_TEMP[i]),
                    pmta(CORE_C0[i]), pmta(CORE_CC1[i]), pmta(CORE_CC6[i]));
        }

        //Don't confuse people by numbering cores that are disabled and hence not shown on 6 | 12 core CPUs
        //(which actually have 8 | 16 cores)
        if (show_disabled_cores || !core_disabled) core_number++;

        //Statistics
        if (!core_disabled) {
            if (peak_core_frequency < core_frequency) peak_core_frequency = core_frequency;
            if (peak_core_temp < pmta(CORE_TEMP[i])) peak_core_temp = pmta(CORE_TEMP[i]);
            if (peak_core_voltage < core_voltage) peak_core_voltage = core_voltage;
            total_core_voltage += core_voltage;
            total_core_power += pmta(CORE_POWER[i]);
            total_usage += pmta(CORE_C0[i]);
            total_core_CC6 += pmta(CORE_CC6[i]);
        }
    }

    fprintf(stdout, "╰─────────┴────────────┴──────────┴─────────┴──────────┴─────────────┴─────────────┴─────────────╯\n");

    fprintf(stdout, "╭── Core Statistics (Calculated) ───────────────┬────────────────────────────────────────────────╮\n");
    print_line("Highest Effective Core Frequency", "%8.0f MHz", peak_core_frequency);
    print_line("Highest Core Temperature", "%8.2f C", peak_core_temp);
    print_line("Highest Core Voltage", "%8.3f V", peak_core_voltage);
    print_line("Average Core Voltage", "%5.3f V", total_core_voltage/sysinfo->enabled_cores_count);

    if (!view_compact) {
        print_line("Average Core CC6", "%6.2f %%", total_core_CC6/sysinfo->enabled_cores_count);
        print_line("Total Core Power Sum", "%7.3f W", total_core_power);
    }

    fprintf(stdout, "├── Reported by SMU ────────────────────────────┼────────────────────────────────────────────────┤\n");
    //print_line("Package Power", "%8.3f W", pmta(SOCKET_POWER)); //Is listed below in power section
    smu_peak_core_voltage = pmta0(CPU_TELEMETRY_VOLTAGE) < peak_core_voltage ? peak_core_voltage : pmta0(CPU_TELEMETRY_VOLTAGE) < 2 ? pmta0(CPU_TELEMETRY_VOLTAGE) : peak_core_voltage;
    print_line("Peak Core Voltage", "%5.3f V", smu_peak_core_voltage);
    if(pmt->PC6) print_line("Package CC6", "%6.2f %%", pmta(PC6));
    fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");

    if (pmt->zen_version == 3 && view_counts && !view_compact) {
        fprintf(stdout, "╭── Curve Optimizer Counts ──────────────────────────────────────────────────────────────────────╮\n");
        int padding, padcount, core_count = 0;
        int count = 0;

        for(k = 1; k <= sysinfo->ccds; k++) {
            padding = 0;
            fprintf(stdout,"│");
            for(l = 0; l < sysinfo->cores_per_ccx; l++) {
                count = op_get_cocount(sysinfo, l, 0);
                count = count > 30 ? 30 : count < -30 ? -30 : count;
                core_disabled = (sysinfo->core_disable_map >> l)&0x01;
                if (!core_disabled) {                    
                    fprintf(stdout," [C%02i: %+3i]", core_count, count);
                    core_count++;
                } else {
                    padding++;
                }
            }
            for (padcount = 0; padcount <= padding; padcount++){
                if (padcount > 0) fprintf(stdout,"           ");
            }
            fprintf(stdout,"        │");
        fprintf(stdout, "\n");
        }
        fprintf(stdout, "╰────────────────────────────────────────────────────────────────────────────────────────────────╯\n");
    }

    if (view_electrical) {
        fprintf(stdout, "╭── Electrical & Thermal Constraints ───────────┬────────────────────────────────────────────────╮\n");
        edc_value = pmta0(EDC_VALUE) * (total_usage / sysinfo->cores / 100);
        if (edc_value < pmta(TDC_VALUE)) edc_value = pmta(TDC_VALUE);

        print_line("Peak Temperature", "%8.2f C", pmta(PEAK_TEMP));
        if(pmt->SOC_TEMP) print_line("SoC Temperature", "%8.2f C", pmta(SOC_TEMP));
        if(pmt->GFX_TEMP) print_line("GFX Temperature", "%8.2f C", pmta(GFX_TEMP));
        //print_line("Core Power", "%8.4f W", pmta(VDDCR_CPU_POWER));

        print_line("Voltage from Core VRM", "%7.3f V | %7.3f V | %8.2f %%", pmta(VID_VALUE), pmta(VID_LIMIT), (pmta(VID_VALUE) / pmta(VID_LIMIT) * 100));
        //if(pmt->STAPM_VALUE) print_line("STAPM", "%7.3f   | %7.f   | %8.2f %%", pmta(STAPM_VALUE), pmta(STAPM_LIMIT), (pmta(STAPM_VALUE) / pmta(STAPM_LIMIT) * 100));
        print_line("PPT", "%7.3f W | %7.f W | %8.2f %%", pmta(PPT_VALUE), pmta(PPT_LIMIT), (pmta(PPT_VALUE) / pmta(PPT_LIMIT) * 100));
        ppt_limit_apu = pmta0(PPT_LIMIT_APU) > 0 ? pmta(PPT_LIMIT_APU) : pmta(PPT_LIMIT);
        if(pmt->PPT_VALUE_APU) print_line("PPT APU", "%7.3f W | %7.f W | %8.2f %%", pmta(PPT_VALUE_APU), ppt_limit_apu, (pmta(PPT_VALUE_APU) / ppt_limit_apu * 100));
        print_line("TDC Value", "%7.3f A | %7.f A | %8.2f %%", pmta(TDC_VALUE), pmta(TDC_LIMIT), (pmta(TDC_VALUE) / pmta(TDC_LIMIT) * 100));
        if(pmt->TDC_ACTUAL) print_line("TDC Actual", "%7.3f A | %7.f A | %8.2f %%", pmta(TDC_ACTUAL), pmta(TDC_LIMIT), (pmta(TDC_ACTUAL) / pmta(TDC_LIMIT) * 100));
        if(pmt->TDC_VALUE_SOC) print_line("TDC Value, SoC only", "%7.3f A | %7.f A | %8.2f %%", pmta(TDC_VALUE_SOC), pmta(TDC_LIMIT_SOC), (pmta(TDC_VALUE_SOC) / pmta(TDC_LIMIT_SOC) * 100));
        print_line("EDC", "%7.3f A | %7.f A | %8.2f %%", edc_value, pmta0(EDC_LIMIT), (edc_value / pmta0(EDC_LIMIT) * 100));
        if(pmt->EDC_VALUE_SOC) print_line("EDC, SoC only", "%7.3f A | %7.f A | %8.2f %%", pmta(EDC_VALUE_SOC), pmta(EDC_LIMIT_SOC), (pmta(EDC_VALUE_SOC) / pmta(EDC_LIMIT_SOC) * 100));
        if (pmt->THM_VALUE) thm_value = pmta(THM_VALUE);
        print_line("THM", "%7.2f C | %7.f C | %8.2f %%", thm_value, pmta(THM_LIMIT), (thm_value / pmta(THM_LIMIT) * 100));
        if (!view_compact) {
            if(pmt->THM_VALUE_SOC) print_line("THM SoC", "%7.2f C | %7.f C | %8.2f %%", pmta(THM_VALUE_SOC), pmta(THM_LIMIT_SOC), (pmta(THM_VALUE_SOC) / pmta(THM_LIMIT_SOC) * 100));
            if(pmt->THM_VALUE_GFX) print_line("THM GFX", "%7.2f C | %7.f C | %8.2f %%", pmta(THM_VALUE_GFX), pmta(THM_LIMIT_GFX), (pmta(THM_VALUE_GFX) / pmta(THM_LIMIT_GFX) * 100));
            //if(pmt->STT_LIMIT_APU) print_line("STT APU", "%7.2f   | %7.f   | %8.2f %%", pmta(STT_VALUE_APU), pmta(STT_LIMIT_APU), (pmta(STT_VALUE_APU) / pmta(STT_LIMIT_APU) * 100)); //Always zero
            //if(pmt->STT_LIMIT_DGPU) print_line("STT DGPU", "%7.2f   | %7.f   | %8.2f %%", pmta(STT_VALUE_DGPU), pmta(STT_LIMIT_DGPU), (pmta(STT_VALUE_DGPU) / pmta(STT_LIMIT_DGPU) * 100)); //Always zero
            print_line("FIT", "%7.f   | %7.f   | %8.2f %%", pmta(FIT_VALUE), pmta(FIT_LIMIT), (pmta(FIT_VALUE) / pmta(FIT_LIMIT)) * 100.f);
        }
        fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");
    }

    if (view_memory) {
        fprintf(stdout, "╭── Memory Interface ───────────────────────────┬────────────────────────────────────────────────╮\n");
        if (!view_compact) {
            print_line("Coupled Mode", "%8s", pmta(UCLK_FREQ) == pmta(MEMCLK_FREQ) ? "ON" : "OFF");
        }
        print_line("Fabric Clock (Average)", "%5.f MHz", pmta(FCLK_FREQ_EFF));
        print_line("Fabric Clock", "%5.f MHz", pmta(FCLK_FREQ));
        if (!view_compact) {
            print_line("Uncore Clock", "%5.f MHz", pmta(UCLK_FREQ));
            print_line("Memory Clock", "%5.f MHz", pmta(MEMCLK_FREQ));
            print_line("cLDO_VDDM", "%7.4f V", pmta(V_VDDM));
            print_line("cLDO_VDDP", "%7.4f V", pmta(V_VDDP));
            if(pmt->V_VDDG)     print_line("cLDO_VDDG", "%7.4f V", pmta(V_VDDG));
            if(pmt->V_VDDG_IOD) print_line("cLDO_VDDG_IOD", "%7.4f V", pmta(V_VDDG_IOD));
            if(pmt->V_VDDG_CCD) print_line("cLDO_VDDG_CCD", "%7.4f V", pmta(V_VDDG_CCD));
        }
        fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");
    }

    if(pmt->has_graphics && view_gfx){
        fprintf(stdout, "╭── Graphics Subsystem ─────────────────────────┬────────────────────────────────────────────────╮\n");
        print_line("GFX Voltage | ROC Power", "%7.4f V | %8.3f W", pmta(GFX_VOLTAGE), pmta(ROC_POWER));
        print_line("GFX Temperature", "%8.2f C", pmta(GFX_TEMP));
        print_line("GFX Clock Real | Effective", "%5.f MHz | %6.f MHz", pmta(GFX_FREQ), pmta(GFX_FREQEFF));
        if (!view_compact) {
            print_line("GFX Busy", "%8.2f %%", pmta(GFX_BUSY) * 100.f);
            if (pmt->GFX_EDC_LIM || pmt->GFX_EDC_RESIDENCY)
                print_line("GFX EDC Limit | Residency", "%7.3f A | %8.2f %%", pmta(GFX_EDC_LIM), pmta(GFX_EDC_RESIDENCY) * 100.f);
            print_line("Display Count | FPS", "%2.f | %8.2f  ", pmta(DISPLAY_COUNT), pmta(FPS));
            print_line("DGPU Power | Freq Target | Busy", "%7.3f W | %5.f MHz | %8.2f %%", pmta0(DGPU_POWER), pmta0(DGPU_FREQ_TARGET), pmta0(DGPU_GFX_BUSY) * 100.f);
        }
        fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");
    }

    if (view_power) {
        fprintf(stdout, "╭── Power Consumption ──────────────────────────┬────────────────────────────────────────────────╮\n");
        //These powers are drawn via VDDCR_SOC and VDDCR_CPU and thus are pulled from the CPU power connector of the mainboard
        print_line("Total Core Power Sum", "%7.3f W", total_core_power);
        //print_line("VDDCR_CPU Power", "%7.3f W", pmta(VDDCR_CPU_POWER)); //This value doesn't correlate with what the cores
                                                                            //report, nor with what is actually consumed. but is
                                                                            //the value HWiNFO shows.
        if(pmt->VDDCR_SOC_POWER)
            print_line("VDDCR_SOC Power", "%7.3f W", pmta(VDDCR_SOC_POWER));
        if (!view_compact) {
            if(pmt->IO_VDDCR_SOC_POWER)
                print_line("IO VDDCR_SOC Power", "%7.3f W", pmta(IO_VDDCR_SOC_POWER));
        }
        if(pmt->ROC_POWER) print_line("ROC Power", "%7.3f W", pmta(ROC_POWER));
        if (!view_compact) {
            if(pmt->GMI2_VDDG_POWER) print_line("GMI2_VDDG Power", "%7.3f W", pmta(GMI2_VDDG_POWER));

            //L3 caches (2 per CCD on Zen2, 1 per CCD on Zen3)
            l3_logic_power=0;
            l3_vddm_power=0;
            for (i=0; i<pmt->max_l3; i++) {
                l3_logic_power += pmta0(L3_LOGIC_POWER[i]);
                l3_vddm_power += pmta0(L3_VDDM_POWER[i]);
            }
            if (pmt->max_l3 == 1) {
                if(pmt->L3_LOGIC_POWER[0])
                    print_line("L3 Logic Power", "%7.3f W", pmta(L3_LOGIC_POWER[0]));
                if(pmt->L3_VDDM_POWER[0])
                    print_line("L3 VDDM Power", "%7.3f W", pmta(L3_VDDM_POWER[0]));
            } else {
                for (i=0; i<pmt->max_l3; i+=2) {
                    // + sign if needed and first value
                    j = snprintf(strbuf, sizeof(strbuf), "%s%7.3f W", (i?"+ ":""), pmta(L3_LOGIC_POWER[i]));
                    // second value if it exists
                    if (pmt->max_l3-i > 1) j += snprintf(strbuf+j, sizeof(strbuf)-j, " + %7.3f W", pmta(L3_LOGIC_POWER[i+1]));
                    // end of string (sum or nothing)
                    if (pmt->max_l3-i > 2) j += snprintf(strbuf+j, sizeof(strbuf)-j, "            ");
                    else j += snprintf(strbuf+j, sizeof(strbuf)-j, " = %7.3f W", l3_logic_power);
                    // print
                    print_line((i?"":"L3 Logic Power"), "%s", strbuf);
                }
                for (i=0; i<pmt->max_l3; i+=2) {
                    // + sign if needed and first value
                    j = snprintf(strbuf, sizeof(strbuf), "%s%7.3f W", (i?"+ ":""), pmta(L3_VDDM_POWER[i]));
                    // second value if it exists
                    if (pmt->max_l3-i > 1) j += snprintf(strbuf+j, sizeof(strbuf)-j, " + %7.3f W", pmta(L3_VDDM_POWER[i+1]));
                    // end of string (sum or nothing)
                    if (pmt->max_l3-i > 2) j += snprintf(strbuf+j, sizeof(strbuf)-j, "            ");
                    else j += snprintf(strbuf+j, sizeof(strbuf)-j, " = %7.3f W", l3_vddm_power);
                    // print
                    print_line((i?"":"L3 VDDM Power"), "%s", strbuf);
                }
            }

            //These powers are supplied by other power lines to the CPU and are drawn from the 24 pin ATX connector on most boards
            print_line("","");
            if(pmt->VDDIO_MEM_POWER && pmta(VDDIO_MEM_POWER) != NAN)
                print_line("VDDIO_MEM Power", "%7.3f W", pmta(VDDIO_MEM_POWER));
            if(pmt->IOD_VDDIO_MEM_POWER && pmta(IOD_VDDIO_MEM_POWER) != NAN)
                print_line("IOD_VDDIO_MEM Power", "%7.3f W", pmta(IOD_VDDIO_MEM_POWER));
            if(pmt->DDR_VDDP_POWER) print_line("DDR_VDDP Power", "%7.3f W", pmta(DDR_VDDP_POWER));
            if(pmt->DDR_PHY_POWER) print_line("DDR Phy Power", "%7.3f W", pmta(DDR_PHY_POWER));
            if(pmt->VDD18_POWER) print_line("VDD18 Power", "%7.3f W", pmta(VDD18_POWER)); //Same as pmta(IO_VDD18_POWER)
            if(pmt->IO_DISPLAY_POWER) print_line("CPU Display IO Power", "%7.3f W", pmta(IO_DISPLAY_POWER));
            if(pmt->IO_USB_POWER) print_line("CPU USB IO Power", "%7.3f W", pmta(IO_USB_POWER));

            if(!pmt->powersum_unclear) {
            //The sum is the thermal output of the whole package. Yes, this is higher than PPT and SOCKET_POWER.
            //Confirmed by measuring the actual current draw on the mainboard.
            print_line("","");
            print_line("Calculated Thermal Output", "%7.3f W", total_core_power + pmta0(VDDCR_SOC_POWER) + pmta0(GMI2_VDDG_POWER) 
                    + l3_logic_power + l3_vddm_power
                    + pmta0(VDDIO_MEM_POWER) + pmta0(IOD_VDDIO_MEM_POWER) + pmta0(DDR_VDDP_POWER) + pmta0(VDD18_POWER));
            }

            if (pmt->SOC_TELEMETRY_VOLTAGE || pmt->SOC_TELEMETRY_CURRENT || pmt->SOC_TELEMETRY_POWER || pmt->CPU_TELEMETRY_VOLTAGE || pmt->CPU_TELEMETRY_CURRENT || pmt->CPU_TELEMETRY_POWER || pmt->VDDCR_CPU_POWER || pmt->SOCKET_POWER || pmt->PACKAGE_POWER)
                fprintf(stdout, "├── Additional Reports ─────────────────────────┼────────────────────────────────────────────────┤\n");
            //print_line("ROC_POWER", "%7.4f",pmta(ROC_POWER));
            if (pmt->SOC_TELEMETRY_VOLTAGE || pmt->SOC_TELEMETRY_CURRENT || pmt->SOC_TELEMETRY_POWER)
                print_line("SoC Power (SVI2)", "%8.3f V | %7.3f A | %8.3f W", pmta0(SOC_TELEMETRY_VOLTAGE), pmta0(SOC_TELEMETRY_CURRENT), pmta0(SOC_TELEMETRY_POWER));
            if (pmt->CPU_TELEMETRY_VOLTAGE || pmt->CPU_TELEMETRY_CURRENT || pmt->CPU_TELEMETRY_POWER || pmt->VDDCR_CPU_POWER)
                print_line("Core Power (SVI2)", "%8.3f V | %7.3f A | %8.3f W", pmta0(CPU_TELEMETRY_VOLTAGE), pmta0(CPU_TELEMETRY_CURRENT), pmta0(CPU_TELEMETRY_POWER));
            if (pmt->VDDCR_CPU_POWER)
                print_line("Core Power (SMU)", "%7.3f W", pmta0(VDDCR_CPU_POWER));
        }
        if (pmt->SOCKET_POWER)
            print_line("Socket Power (SMU)", "%7.3f W", pmta0(SOCKET_POWER));
        if (!view_compact) {
            if (pmt->PACKAGE_POWER) print_line("Package Power (SMU)", "%7.3f W", pmta0(PACKAGE_POWER));
        }
        fprintf(stdout, "╰───────────────────────────────────────────────┴────────────────────────────────────────────────╯\n");
    }
}

void remove_spaces(char* s) {
    char* d = s;
    do {
        while (*d == ' ') {
            ++d;
        }
    } while (*s++ = *d++);
}

void draw_export(pm_table *pmt, system_info *sysinfo) {
    //general
    int i, j, k, l;
    //core block
    float core_voltage, core_frequency, package_sleep_time, core_sleep_time, average_voltage;
    float peak_core_frequency, peak_core_temp, peak_core_voltage;
    float total_core_voltage, total_core_power, total_usage, total_core_CC6;
    int core_disabled, core_number;
    float thm_value = 0;
    //constraints block
    float edc_value;
    //power block
    float l3_logic_power, l3_vddm_power;
    char strbuf[100];

    char hostname[HOST_NAME_MAX + 1];
    gethostname(hostname, HOST_NAME_MAX + 1);    
    remove_spaces(hostname);
    
    peak_core_frequency = peak_core_temp = peak_core_voltage = 0;
    total_core_voltage = total_core_power = total_usage = total_core_CC6 = 0;
    core_number = 0;

    for (i = 0; i < pmt->max_cores; i++) {
        core_disabled = (sysinfo->core_disable_map >> i)&0x01;
        average_voltage = i > 0 ? (average_voltage+pmta(CORE_VOLTAGE[i]))/2 : pmta(CORE_VOLTAGE[i]);
        if (!core_disabled) core_number++;
    }

    core_number = 0;

    if (!average_voltage > 0)
        average_voltage = pmta(CPU_TELEMETRY_VOLTAGE);

    if(pmt->PC6)
    {
        package_sleep_time = pmta(PC6) / 100.f;
        average_voltage = ((average_voltage) - (0.2 * package_sleep_time)) / (1.0 - package_sleep_time);
    }

    // Cores

    for (i = 0; i < pmt->max_cores; i++) {
        core_disabled = (sysinfo->core_disable_map >> i)&0x01;
        core_frequency = pmta0(CORE_FREQEFF[i]) * 1000.f;

        if (!pmt->THM_VALUE) {
            if (pmta0(THM_VALUE_CORES[i]) > 0 && pmta0(THM_VALUE_CORES[i]) > thm_value)
                thm_value = pmta0(THM_VALUE_CORES[i]);
        }

        core_voltage = pmta0(CORE_VOLTAGE[i]); // True core voltage
        // Rumours say this is how AMD calculates core voltage
        //if (pmta(CORE_FREQ[i]) != 0.f) {
        core_sleep_time = pmta0(CORE_CC6[i]) / 100.f;
        if (core_sleep_time > 0 && average_voltage < 2)
            core_voltage = ((1.0 - core_sleep_time) * average_voltage) + (0.2 * core_sleep_time);
        //}

        if (core_disabled) {
            if (show_disabled_cores)
                    fprintf(stdout,
                        "ryzen_monitor_ng,host=%s,name=%s%d core_state=\"Disabled\",core_sleeping=1i,core_active=0i,core_power=%.3f,core_vid=%.3f,core_temperature=%.2f,core_c0=%.1f,core_c1=%.1f,core_c6=%.1f\n",
                    hostname, "Core", core_number,
                        pmta0(CORE_POWER[i]), core_voltage, pmta0(CORE_TEMP[i]),
                        pmta0(CORE_C0[i]), pmta0(CORE_CC1[i]), pmta0(CORE_CC6[i]));
        }
        else {
            char *state = "Sleeping";
            int sleeping = 1;
            int active = 0;
            if (pmta(CORE_C0[i]) >= 6.f) {
                // AMD denotes a sleeping core as having spent less than 6% of the time in C0.
                // Source: Ryzen Master
                state = "Active";
                sleeping = 0;
                active = 1;
            }
            fprintf(stdout,
                    "ryzen_monitor_ng,host=%s,name=%s%d core_state=\"%s\",core_sleeping=%ii,core_active=%ii,core_frequency=%.0fi,core_power=%.3f,core_vid=%.3f,core_temperature=%.2f,core_c0=%.1f,core_c1=%.1f,core_c6=%.1f\n",
                    hostname, "Core", core_number, state, sleeping, active,
                    core_frequency, pmta0(CORE_POWER[i]), core_voltage, pmta0(CORE_TEMP[i]),
                    pmta0(CORE_C0[i]), pmta0(CORE_CC1[i]), pmta0(CORE_CC6[i]));
        }

        //Don't confuse people by numbering cores that are disabled and hence not shown on 6 | 12 core CPUs
        //(which actually have 8 | 16 cores)
        if (show_disabled_cores || !core_disabled) core_number++;

        //Statistics
        if (!core_disabled) {
            if (peak_core_frequency < core_frequency) peak_core_frequency = core_frequency;
            if (peak_core_temp < pmta0(CORE_TEMP[i])) peak_core_temp = pmta0(CORE_TEMP[i]);
            if (peak_core_voltage < core_voltage) peak_core_voltage = core_voltage;
            total_core_voltage += core_voltage;
            total_core_power += pmta0(CORE_POWER[i]);
            total_usage += pmta0(CORE_C0[i]);
            total_core_CC6 += pmta0(CORE_CC6[i]);
        }
    }

    fprintf(stdout,
            "ryzen_monitor_ng,host=%s,name=Cores ", hostname);
    fprintf(stdout,
            "cores_maxfrequencyeff=%.0fi,", peak_core_frequency);
    fprintf(stdout,
            "cores_maxtemperature=%.2f,", peak_core_temp);
    fprintf(stdout,
            "cores_maxvid=%.3f,", peak_core_voltage);
    fprintf(stdout,
            "cores_avgvid=%.3f,", total_core_voltage/sysinfo->enabled_cores_count);
    fprintf(stdout,
            "cores_avgcc6=%.2f,", total_core_CC6/sysinfo->enabled_cores_count);
    fprintf(stdout,
            "cores_totalpower=%.3f,", total_core_power);
    if(pmt->PC6)
        fprintf(stdout,
                "package_cc6=%.2f,", pmta0(PC6));
    fprintf(stdout,
            "cpu_maxvid_smu=%.3f", pmta0(CPU_TELEMETRY_VOLTAGE));
    fprintf(stdout,
            "\n");

    if (pmt->zen_version == 3) {
        int padding, padcount, core_count = 0;
        int count = 0;

        for(k = 1; k <= sysinfo->ccds; k++) {
            padding = 0;
            for(l = 0; l < sysinfo->cores_per_ccx; l++) {
                count = op_get_cocount(sysinfo, l, 0);
                count = count > 30 ? 30 : count < -30 ? -30 : count;
                core_disabled = (sysinfo->core_disable_map >> l)&0x01;
                if (!core_disabled) {                    
                    fprintf(stdout,"ryzen_monitor_ng,host=%s,name=%s%d core_psmcount=%ii,core%i_psmcount=%ii", hostname, "Core", core_count, count, core_count, count);
                    fprintf(stdout, "\n");
                    core_count++;
                }
            }
        }
    }

    // Package values

    fprintf(stdout,
            "ryzen_monitor_ng,host=%s,name=Package ", hostname);

    edc_value = pmta0(EDC_VALUE) * (total_usage / sysinfo->cores / 100);
    if (edc_value < pmta0(TDC_VALUE)) edc_value = pmta0(TDC_VALUE);

    fprintf(stdout,
            "package_peaktemperature=%.2f,", pmta0(PEAK_TEMP));

    if(pmt->SOC_TEMP)
        fprintf(stdout,
            "soc_temperature=%.2f,", pmta0(SOC_TEMP));

    if(pmt->GFX_TEMP)
        fprintf(stdout,
            "gfx_temperature=%.2f,", pmta0(GFX_TEMP));

    fprintf(stdout,
            "package_vcorevrm_vid=%.3f,", pmta0(VID_VALUE));

    fprintf(stdout,
            "package_vcorevrm_vidlimit=%.3f,", pmta0(VID_LIMIT));

    if(pmt->STAPM_VALUE) {
        fprintf(stdout,
                "cpu_stapm=%.3f,", pmta0(STAPM_VALUE));
        fprintf(stdout,
                "cpu_stapmlimit=%.fi,", pmta0(STAPM_LIMIT));
    }
    
    fprintf(stdout,
            "cpu_ppt=%.3f,", pmta0(PPT_VALUE));
    fprintf(stdout,
            "cpu_pptlimit=%.fi,", pmta0(PPT_LIMIT));

    if(pmt->PPT_VALUE_APU) {
        fprintf(stdout,
                "cpu_pptapu=%.3f,", pmta0(PPT_VALUE_APU));
        fprintf(stdout,
                "cpu_pptapulimit=%.fi,", pmta0(PPT_LIMIT_APU));
    }

    fprintf(stdout,
            "cpu_tdc=%.3f,", pmta0(TDC_VALUE));
    fprintf(stdout,
            "cpu_tdclimit=%.fi,", pmta0(TDC_LIMIT));

    if(pmt->TDC_ACTUAL) {
        fprintf(stdout,
                "cpu_tdcactual=%.3f,", pmta0(TDC_ACTUAL));
    }

    if(pmt->TDC_VALUE_SOC) {
        fprintf(stdout,
                "soc_tdc=%.3f,", pmta0(TDC_VALUE_SOC));
        fprintf(stdout,
                "soc_tdclimit=%.fi,", pmta0(TDC_LIMIT_SOC));
    }

    fprintf(stdout,
            "cpu_edc=%.3f,", pmta0(EDC_VALUE));
    fprintf(stdout,
            "cpu_edclimit=%.fi,", pmta0(EDC_LIMIT));

    if(pmt->EDC_VALUE_SOC) {
        fprintf(stdout,
                "soc_edc=%.3f,", pmta0(EDC_VALUE_SOC));
        fprintf(stdout,
                "soc_edclimit=%.fi,", pmta0(EDC_LIMIT_SOC));
    }

    if (pmt->THM_VALUE) thm_value = pmta(THM_VALUE);

    fprintf(stdout,
            "cpu_thm=%.2f,", thm_value);

    fprintf(stdout,
            "cpu_thmlimit=%.0fi,", pmta0(THM_LIMIT));

    if(pmt->THM_VALUE_SOC) {
        fprintf(stdout,
                "soc_thmsoc=%.2f,", pmta0(THM_VALUE_SOC));
        fprintf(stdout,
                "soc_thmlimit=%.0fi,", pmta0(THM_LIMIT_SOC));
    }

    if(pmt->THM_VALUE_GFX) {
        fprintf(stdout,
                "gfx_thm=%.2f,", pmta0(THM_VALUE_GFX));
        fprintf(stdout,
                "gfx_thmlimit=%.0fi,", pmta0(THM_LIMIT_GFX));
    }

    if(pmt->STT_LIMIT_APU && pmta0(STT_LIMIT_APU) > 0) {
        fprintf(stdout,
                "cpu_sttapu=%.3f,", pmta0(STT_VALUE_APU));
        fprintf(stdout,
                "cpu_sttapulimit=%.fi,", pmta0(STT_LIMIT_APU));
    }

    if(pmt->STT_LIMIT_DGPU && pmta0(STT_LIMIT_DGPU) > 0) {
        fprintf(stdout,
                "gfx_sttdgpu=%.3f,", pmta0(STT_VALUE_DGPU));
        fprintf(stdout,
                "gfx_sttdgpulimit=%.fi,", pmta0(STT_LIMIT_DGPU));
    }

    fprintf(stdout,
            "cpu_fit=%.0fi,", pmta0(FIT_VALUE));

    fprintf(stdout,
            "cpu_fitlimit=%.0fi", pmta0(FIT_LIMIT));

    fprintf(stdout,
            "\n");

    //Fabric & IO

    fprintf(stdout,
            "ryzen_monitor_ng,host=%s,name=FabricIO ", hostname);

    if(pmt->V_VDDM && pmta0(V_VDDM) > 0)
        fprintf(stdout,
                "cpu_vddm=%.4f,", pmta0(V_VDDM));

    if(pmt->V_VDDP && pmta0(V_VDDP) > 0)
        fprintf(stdout,
                "cpu_vddp=%.4f,", pmta0(V_VDDP));

    if(pmt->V_VDDG && pmta0(V_VDDG) > 0)
        fprintf(stdout,
                "cpu_vddg=%.4f,", pmta0(V_VDDG));

    if(pmt->V_VDDG_IOD && pmta0(V_VDDG_IOD) > 0)
        fprintf(stdout,
                "cpu_vddg_iod=%.4f,", pmta0(V_VDDG_IOD));

    if(pmt->V_VDDG_CCD && pmta0(V_VDDG_CCD) > 0)
        fprintf(stdout,
                "cpu_cldo_vddg_ccd=%.4f,", pmta0(V_VDDG_CCD));

    fprintf(stdout,
            "cpu_coupled=%s,", pmta0(UCLK_FREQ) == pmta0(MEMCLK_FREQ) ? "true" : "false");

    fprintf(stdout,
            "cpu_fabricclockeff=%.0f,", pmta0(FCLK_FREQ_EFF));

    fprintf(stdout,
            "cpu_fabricclock=%.0f,", pmta0(FCLK_FREQ));

    fprintf(stdout,
            "cpu_uncoreclock=%.0f,", pmta0(UCLK_FREQ));

    fprintf(stdout,
            "cpu_memoryclock=%.0f", pmta0(MEMCLK_FREQ));

    fprintf(stdout,
            "\n");

    //GFX
    if(pmt->has_graphics){

        fprintf(stdout,
                "ryzen_monitor_ng,host=%s,name=GFX ", hostname);

        fprintf(stdout,
                "gfx_voltage=%.4f,", pmta0(GFX_VOLTAGE));

        fprintf(stdout,
                "gfx_rocpower=%.3f,", pmta0(ROC_POWER));

        fprintf(stdout,
                "gfx_temperature=%.2f,", pmta0(GFX_TEMP));

        fprintf(stdout,
                "gfx_clock=%.0f,", pmta0(GFX_FREQ));

        fprintf(stdout,
                "gfx_clockeff=%.0fi,", pmta0(GFX_FREQEFF));
        
        fprintf(stdout,
                "gfx_busy=%.2f,", pmta0(GFX_BUSY) * 100.f);
        
        fprintf(stdout,
                "gfx_edclimit=%.0fi,", pmta0(GFX_EDC_LIM));

        fprintf(stdout,
                "gfx_residency=%.2f,", pmta0(GFX_EDC_RESIDENCY) * 100.f);
        
        fprintf(stdout,
                "gfx_displaycount=%.0f,", pmta0(DISPLAY_COUNT));
        
        fprintf(stdout,
                "gfx_fps=%.2f,", pmta0(FPS));

        fprintf(stdout,
                "gfx_dgpupower=%.3f,", pmta0(DGPU_POWER));

        fprintf(stdout,
                "gfx_dgpuclocktarget=%.0fi,", pmta0(DGPU_FREQ_TARGET));

        fprintf(stdout,
                "gfx_dgpubusy=%.2f", pmta0(DGPU_GFX_BUSY) * 100.f);

        fprintf(stdout,
                "\n");
    }

    //Package power

    fprintf(stdout,
            "ryzen_monitor_ng,host=%s,name=Package ", hostname);

    fprintf(stdout,
            "package_vddcrcpupower=%.3f,", pmta0(VDDCR_CPU_POWER));

    fprintf(stdout,
            "package_vddcrsocpower=%.3f,", pmta0(VDDCR_SOC_POWER));

    if(pmt->IO_VDDCR_SOC_POWER) 
        fprintf(stdout,
            "package_vddcriosocpower=%.3f,", pmta0(IO_VDDCR_SOC_POWER));

    if(pmt->GMI2_VDDG_POWER) 
        fprintf(stdout,
            "package_gmi2vddgpower=%.3f,", pmta0(GMI2_VDDG_POWER));

    //L3 caches (2 per CCD on Zen2, 1 per CCD on Zen3)
    l3_logic_power=0;
    l3_vddm_power=0;
    for (i=0; i<pmt->max_l3; i++) {
        l3_logic_power += pmta0(L3_LOGIC_POWER[i]);
        l3_vddm_power += pmta0(L3_VDDM_POWER[i]);
    }
    if (pmt->max_l3 == 1) {

        fprintf(stdout,
            "package_l3logicpower=%.3f,", pmta0(L3_LOGIC_POWER[0]));

        fprintf(stdout,
            "package_l3vddmpower=%.3f,", pmta0(L3_VDDM_POWER[0]));

    } else {

        for (i=0; i<pmt->max_l3; i+=2) {
            // first value
            j = snprintf(strbuf, sizeof(strbuf), "package_l3logic%spower=%.3f,", i, pmta0(L3_LOGIC_POWER[i]));
            // second value if it exists
            if (pmt->max_l3-i > 1) j += snprintf(strbuf+j, sizeof(strbuf)-j, "package_l3logic%spower=%.3f,", i+1, pmta0(L3_LOGIC_POWER[i+1]));
            // end of string (sum or nothing)
            if (pmt->max_l3-i <= 3)
                j += snprintf(strbuf+j, sizeof(strbuf)-j, "package_l3logicpower=%.3f,", l3_logic_power);
            // print
            fprintf(stdout,
                "%s", strbuf);
        }
        for (i=0; i<pmt->max_l3; i+=2) {
            // + sign if needed and first value
            j = snprintf(strbuf, sizeof(strbuf), "package_l3vddm%spower=%.3f,", i, pmta0(L3_VDDM_POWER[i]));
            // second value if it exists
            if (pmt->max_l3-i > 1) j += snprintf(strbuf, sizeof(strbuf)-j, "package_l3vddm%spower=%.3f,", i+1, pmta0(L3_VDDM_POWER[i+1]));
            // end of string (sum or nothing)
            if (pmt->max_l3-i <= 3)
                j += snprintf(strbuf+j, sizeof(strbuf)-j, "package_l3vddmpower=%.3f,", l3_vddm_power);
            // print
            fprintf(stdout,
                "%s", strbuf);
        }
    }

    //These powers are supplied by other power lines to the CPU and are drawn from the 24 pin ATX connector on most boards

    fprintf(stdout,
        "package_vddiomempowerr=%.3f,", pmta0(VDDIO_MEM_POWER));

    fprintf(stdout,
        "package_iodvddiomempowerr=%.3f,", pmta0(IOD_VDDIO_MEM_POWER));

    if(pmt->DDR_VDDP_POWER) 
        fprintf(stdout,
            "package_ddrvddppower=%.3f,", pmta0(DDR_VDDP_POWER));
            
    if(pmt->DDR_PHY_POWER) 
        fprintf(stdout,
            "package_ddrphypower=%.3f,", pmta0(DDR_PHY_POWER));            

    if(pmt->IO_DISPLAY_POWER) 
        fprintf(stdout,
            "package_displayiopower=%.3f,", pmta0(IO_DISPLAY_POWER));
            
    if(pmt->IO_USB_POWER) 
        fprintf(stdout,
            "package_usbiopower=%.3f,", pmta0(IO_USB_POWER));

    if(!pmt->powersum_unclear) {
        //The sum is the thermal output of the whole package. Yes, this is higher than PPT and SOCKET_POWER.
        //Confirmed by measuring the actual current draw on the mainboard.
        fprintf(stdout,
            "package_calc_thermaloutput=%.3f,", total_core_power + pmta0(VDDCR_SOC_POWER) + pmta0(GMI2_VDDG_POWER) 
            + l3_logic_power + l3_vddm_power
            + pmta0(VDDIO_MEM_POWER) + pmta0(IOD_VDDIO_MEM_POWER) + pmta0(DDR_VDDP_POWER) + pmta0(VDD18_POWER));
    }

    fprintf(stdout,
        "package_vi2socvoltage=%.3f,", pmta0(SOC_TELEMETRY_VOLTAGE));

    fprintf(stdout,
        "package_svi2soccurrent=%.3f,", pmta0(SOC_TELEMETRY_CURRENT));

    fprintf(stdout,
        "package_svi2socpower=%.3f,", pmta0(SOC_TELEMETRY_POWER));

    fprintf(stdout,
        "package_svi2cpuvoltage=%.3f,", pmta0(CPU_TELEMETRY_VOLTAGE));

    fprintf(stdout,
        "package_svi2cpucurrent=%.3f,", pmta0(CPU_TELEMETRY_CURRENT));

    fprintf(stdout,
        "package_svi2cpupower=%.3f,", pmta0(CPU_TELEMETRY_POWER));

    fprintf(stdout,
        "package_smusocketpower=%.3f,", pmta0(SOCKET_POWER));

    if(pmt->PACKAGE_POWER) 
        fprintf(stdout,
            "package_smupackagepower=%.3f,", pmta0(PACKAGE_POWER));

    fprintf(stdout,
        "package_vdd18power=%.3f,", pmta0(VDD18_POWER));

    fprintf(stdout,
            "package_totalcorepower=%.3f", total_core_power);

    fprintf(stdout,
            "\n");
    
}

void disabled_cores_from_pmt(pm_table *pmt, system_info *sysinfo) {
    int i, mask;
    float power, voltage, fit, iddmax, freq, freqeff, c0, cc1, irm;
    for (i = 0; i < pmt->max_cores; i++) {
        power = pmta0(CORE_POWER[i]);
        voltage = pmta0(CORE_VOLTAGE[i]);
        fit = pmta0(CORE_FIT[i]);
        iddmax = pmta0(CORE_IDDMAX[i]);
        freq = pmta0(CORE_FREQ[i]);
        freqeff = pmta0(CORE_FREQEFF[i]);
        c0 = pmta0(CORE_C0[i]);
        cc1 = pmta0(CORE_CC1[i]);
        irm = pmta0(CORE_IRM[i]);
        
        if (power == 0 && voltage == 0 && fit == 0 && iddmax == 0 && freq == 0 && freqeff == 0 && c0 == 0 && cc1 == 0 && irm == 0 ) {
            mask = 1 << i;
        } else {
            mask = 0 << i;
        }
        sysinfo->core_disable_map_pmt = sysinfo->core_disable_map_pmt | mask;
    }
}

int kbhit(void)
{
  struct termios oldt, newt;
  int ch;
  int oldf;
 
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
 
  ch = getchar();
 
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  fcntl(STDIN_FILENO, F_SETFL, oldf);
 
  if(ch != EOF)
  {
    ungetc(ch, stdin);
    return 1;
  }
 
  return 0;
}

int init_pmt(pm_table* pmt, unsigned int force) {
    unsigned char* pm_buf;

    if (!smu_pm_tables_supported(&obj)) {
        fprintf(stderr, "PM Tables are not supported on this platform.\n");
        return -100;
    }

    pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
    if (!pm_buf) {
        fprintf(stderr, "Could not allocate memory for the PM Table.\n");
        return -101;
    }

    if (force) {
        fprintf(stderr, "Forcing to use PM Table version 0x%x. System reports version 0x%x.\n", force, obj.pm_table_version);
    }

    //Select matching PM Table
    if (!select_pm_table_version(force ? force : obj.pm_table_version, pmt, pm_buf)) {
        fprintf(stderr, "This PM Table version (0x%x) is currently not supported.\n", force ? force : obj.pm_table_version);
        fprintf(stderr, "Processor name: %s\n", get_processor_name());
        fprintf(stderr, "SMU FW version: %s\n", smu_get_fw_version(&obj));
        return -102;
    }
    //Prevent illegal memory access
    if (obj.pm_table_size < pmt->min_size) {
        fprintf(stderr, "Selected PM Table is larger than the PM Table returned by the SMU.\n");
        return -103;
    }

    free(pm_buf);
    return 0;
}

int init_sysinfo(pm_table* pmt, system_info* sysinfo) {
    unsigned char* pm_buf;
    int pmt_hack_fuse = 0;

    sysinfo->enabled_cores_count = 1;

    sysinfo->cpu_name    = get_processor_name();
    sysinfo->codename    = smu_codename_to_str(&obj);
    sysinfo->smu_codename= obj.codename;
    sysinfo->smu_fw_ver  = smu_get_fw_version(&obj);

    //PMT hack for core_disabled_map 
    if (smu_pm_tables_supported(&obj)) {
        pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
        if (smu_read_pm_table(&obj, pm_buf, obj.pm_table_size) == SMU_Return_OK) {
            disabled_cores_from_pmt(pmt, sysinfo);
        }
        free(pm_buf);
    }
   
    get_processor_topology(sysinfo);

    switch (obj.smu_if_version) {
        case IF_VERSION_9:  sysinfo->if_ver =  9; break;
        case IF_VERSION_10: sysinfo->if_ver = 10; break;
        case IF_VERSION_11: sysinfo->if_ver = 11; break;
        case IF_VERSION_12: sysinfo->if_ver = 12; break;
        case IF_VERSION_13: sysinfo->if_ver = 13; break;
        default:            sysinfo->if_ver =  0; break;
    }
   
    return 0;
}

int start_pm_export() {
    unsigned char* pm_buf;
    int err = 0;
    int ret;
    
    pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
    select_pm_table_version(obj.pm_table_version, &pmt, pm_buf);

    //export influx line protocol format for telegraf
    signal(SIGPIPE, SIG_IGN);
    ret = mkfifo(pm_export_pipe, 0666);
    if (ret != 0) {
        remove(pm_export_pipe);
        ret = mkfifo(pm_export_pipe, 0666);
    }
    if (ret == 0) {
        fdpipe = open(pm_export_pipe, O_WRONLY);
        if (fdpipe == -1) {
            fprintf(stderr, "Can't open named pipe for writing export\n");
            err = -640;
        }
        else if (fdpipe > 0) {
            close(fdpipe);
            setvbuf(stdout, NULL, _IONBF, 0);
            while (1) {
                fdpipe = open(pm_export_pipe, O_WRONLY);
                dup2(fdpipe, 1);
                if (smu_read_pm_table(&obj, pm_buf, obj.pm_table_size) != SMU_Return_OK)
                    continue;
                draw_export(&pmt, &sysinfo);
                fflush(NULL);
                close(fdpipe);
                sleep(export_update_time_s);
            }
        }
        close(fdpipe);
        remove(pm_export_pipe);
    }
    else {
        fprintf(stderr, "Can't create named pipe for export\n");
        err = -512;
    }

    fflush(stdout);
    fflush(stderr);

    free(pm_buf);
    return err;
}

void start_pm_monitor(unsigned int force, unsigned int test_export) {
    unsigned char *pm_buf;
    int exit_loop = 0;

    pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
    select_pm_table_version(force?force:obj.pm_table_version, &pmt, pm_buf);

    int kpress;
    int update_time_ms = update_time_s*1000;
    int restupdate = 0, draw_update = 0;
    int sleepms = 200;

    fprintf(stdout, "\e[2J\e[1;1H"); //Clear entire screen;Move cursor to (1,1) 
    fprintf(stdout, "\e[?25l"); // Hide Cursor


    while(exit_loop == 0){
            
        if (kbhit()){
            sleepms = 0;
            kpress = getchar();
            if (kpress == 113 || kpress == 81) exit_loop = 1;
            if (kpress == 99  || kpress == 67) view_compact ^= 1;
            if (kpress == 105 || kpress == 73) view_info ^= 1;
            if (kpress == 111 || kpress == 79) view_counts ^= 1;
            if (kpress == 101 || kpress == 69) view_electrical ^= 1;
            if (kpress == 109 || kpress == 67) view_memory ^= 1;
            if (kpress == 103 || kpress == 71) view_gfx ^= 1;
            if (kpress == 112 || kpress == 80) view_power ^= 1;
            sleepms = 200;
            draw_update = 1;
            fprintf(stdout, "\e[2J\e[1;1H"); //Clear entire screen;Move cursor to (1,1) 
        }


        if (restupdate <= 0 || draw_update) {
            if (smu_read_pm_table(&obj, pm_buf, obj.pm_table_size) == SMU_Return_OK) {
                msleep(sleepms);

                fprintf(stdout, "\e[1;1H"); //Move cursor to (1,1) 
                if (test_export) {
                    fprintf(stdout, "\e[2J\e[1;1H"); //Clear entire screen;Move cursor to (1,1) 
                    draw_export(&pmt, &sysinfo);
                } else {
                    draw_screen(&pmt, &sysinfo);
                }
                fflush(stdout);

                draw_update = 0;
            }
        }
            
        if (restupdate <= 0) restupdate = update_time_ms;
        restupdate -= sleepms;
        msleep(sleepms);
            
    }
    fprintf(stdout, "\e[?25h"); // Unhide Cursor

}

void read_from_dumpfile(char *dumpfile, unsigned int version, unsigned int test_export, unsigned int dump_table) {
    unsigned char readbuf[10240];
    unsigned char dumpbuf[10240];
    unsigned int bytes_read;
    int i;
    float v;
    pm_table pmt;
    system_info sysinfo;
    FILE *fd;

    if (!version) {
        fprintf(stderr, "You need to specify a PM Table version with -f.\n");
        exit(0);
    }

    //Read file
    fd = fopen(dumpfile, "rb");
    if(!fd) {
        fprintf(stderr, "Could not read the dumpfile (\"%s\").\n", dumpfile);
        exit(0);
    }
    bytes_read=fread(readbuf,sizeof(char),sizeof(readbuf),fd);
    fclose(fd);

    //Select matching PM Table
    if(!select_pm_table_version(version, &pmt, readbuf)) {
        fprintf(stderr, "This PM Table version (0x%x) is currently not supported.\n", version);
        exit(0);
    }
    else fprintf(stderr, "Using PM Table version 0x%x.\n", version);

    //Prevent illegal memory access
    if (bytes_read < pmt.min_size) {
        fprintf(stderr, "Read %d bytes from \"%s\", but the selected PM Table is %d bytes long.\n", bytes_read, dumpfile, pmt.min_size);
        exit(0);
    }
    
    sysinfo.available=0; //Did not read sysinfo
    sysinfo.enabled_cores_count = pmt.max_cores;
    sysinfo.core_disable_map=0;
    sysinfo.cores=sysinfo.enabled_cores_count;

    if (test_export)
        draw_export(&pmt, &sysinfo);
    else
        draw_screen(&pmt, &sysinfo);

    if (dump_table) {
        fprintf(stdout, "\n\n");
        fd = fopen(dumpfile, "rb");
        //fread(dumpbuf,sizeof(char),sizeof(dumpbuf),fd);
        i = 0;
        while (1){
            fread((void*)(&v), sizeof(v), 1, fd);
            if( feof(fd) ) 
                break ;
            fprintf(stdout, "%i\t%f\n", i, v );
            i++;
        }
        fclose(fd);
    }

    fprintf(stdout, "\e[?25h"); // Unhide Cursor

}

int write_to_dumpfile(char *dumpfile) {
    char *buf = (char*) malloc(BUF_SIZE* sizeof(char));
    unsigned int bytes_written = 0;
    size_t in, out;
    FILE *src, *dst;
    char* pmtv = malloc(sizeof(obj.pm_table_version)+1);
    sprintf(pmtv, "%X_", obj.pm_table_version);
    char* filename = (char*)malloc(sizeof(pmtv) + sizeof(dumpfile));
    strcat(filename, pmtv);
    strcat(filename, dumpfile);

    src = fopen("/sys/kernel/ryzen_smu_drv/pm_table", "rb");
    dst = fopen(filename, "wb");

    if(!dst) {
        fprintf(stderr, "Could not create the dumpfile (\"%s\").\n", filename);
        return -4;
    } else if (!src){
        fprintf(stderr, "Could not read the PM table from kernel.\n");
        return -4;
    } else {
        while (1) {
            in = fread(buf, sizeof(char), BUF_SIZE, src);
            if (0 == in) break;
            out = fwrite(buf, sizeof(char), in, dst);
            bytes_written += (int)out;
            if (0 == out) break;
        }
        fprintf(stdout, "Written %i bytes in dumpfile (\"%s\").\n", bytes_written, filename);
    }
    free(pmtv);
    free(filename);
    return 0;
}


void print_version() {
    fprintf(stdout, "Ryzen Monitor v" PROGRAM_VERSION " (NG)\n");
    exit(0);
}

void signal_interrupt(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGABRT:
        case SIGTERM:
           // Re-enable the cursor.
           fprintf(stdout, "\e[?25h");
           smu_free(&obj); 
           if (fdpipe != 0) {
               close(fdpipe);
               remove(pm_export_pipe);
           }
           exit(0);
        default:
            break;
    }
}

static const char *const usage[] = {
        "ryzen_monitor [options]",
        NULL,
};

int set_cmdmode(struct argparse *self,
                         const struct argparse_option *option)
{
    (void)option;
    cmd_mode = 1;
    return (EXIT_SUCCESS);
}

int isvalid_coreidstr(const char* core){
    errno = 0;
    char *leftover;
    long val = strtol(core, &leftover, 10);
    if (leftover == core || *leftover != '\0' || val < 0 || val >= sysinfo.cores ||
        ((*leftover == INT_MIN || *leftover == INT_MAX) && errno == ERANGE)) {
        return -1;
    } else {
        return (int)val;
    }
}

int main(int argc, const char** argv) {
    smu_return_val ret;

    int helpinfo=0, versioninfo=0, memorytimings=0, err=0, skip=0, cmdmode=0;
    int printtimings=0, force_update_time_s=0, test_export=0;
    int forcetable=0, dumptable=0;
    int tview_compact=0, tview_info=0, tview_counts=0, tview_electrical=0, tview_memory=0, tview_gfx=0, tview_power=0;
    int set_enable_oc=0, set_disable_oc=0, get_ocmode=0, set_enable_eco=0, set_enable_maxperf=0;
    int get_ppt=0, get_pptfast=0, get_pptapu=0, get_tdc=0, get_tdcsoc=0, get_edc=0, get_edcsoc=0, get_stapm=0, get_ppt_time=0, get_stapm_time=0, get_thm=0, get_scalar=0, get_cocountall=0;
    int set_ppt=0, set_pptfast=0, set_pptapu=0, set_tdc=0, set_tdcsoc=0, set_edc=0, set_edcsoc=0, set_stapm=0, set_ppt_time=0, set_stapm_time=0, set_thm=0, set_scalar=0, set_cocountall=0;

    char *set_cocount = NULL;
    char *dumpfile = NULL;
    char *writedump = NULL;
    char *forcetablestr = NULL;
    char *get_cocount = NULL;
 
    //Set up signal handlers
    if ((signal(SIGABRT, signal_interrupt) == SIG_ERR) ||
        (signal(SIGTERM, signal_interrupt) == SIG_ERR) ||
        (signal(SIGINT, signal_interrupt) == SIG_ERR)) {
        fprintf(stderr, "Can't set up signal hooks.\n");
        exit(-1);
    }

    struct argparse_option options[] = {
            OPT_HELP(),
            OPT_GROUP("Options"),
            OPT_BOOLEAN('h', "help", &helpinfo, "Show this help screen."),
            OPT_BOOLEAN('v', "version", &versioninfo, "Show program version."),
            OPT_BOOLEAN('m', "timings", &printtimings, "Print DRAM Timings and exit."),
            OPT_BOOLEAN('d', "disabled", &show_disabled_cores, "Show disabled cores."),
            OPT_INTEGER('u', "update", &force_update_time_s, "Update refresh for monitoring, in seconds. Defaults to 1."),
            OPT_STRING('t', "dumpfile", &dumpfile, "Test mode, Read PM Table from raw-dumpfile. Must be used with -f."),
            OPT_STRING('w', "writedump", &writedump, "Write PM Table to raw-dumpfile. PM Table version will prepend the filename."),
            OPT_STRING('f', "forcetable", &forcetablestr, "Force to use a specific PM table version (Hex value)."),
            OPT_BOOLEAN('\0', "dumptable", &dumptable, "Dump table on screen. Can be used with -t."),
            OPT_STRING('e', "export", &pm_export_pipe, "Export metrics mode to a named pipe, Influx inline protocol."),
            OPT_BOOLEAN('\0', "debuglog", &debuglog, "Print out debug error messages."),
            OPT_BOOLEAN('\0', "test-export", &test_export, "Export metrics mode to console for testing purpose, can be used with a raw-dumpfile."),
            OPT_BOOLEAN('c', "compact", &tview_compact, "Toggle compact view in monitor."),
            OPT_BOOLEAN('\0', "t-info", &tview_info, "Toggle view Info in monitor."),
            OPT_BOOLEAN('\0', "t-counts", &tview_counts, "Toggle view Counts in monitor."),
            OPT_BOOLEAN('\0', "t-electrical", &tview_electrical, "Toggle view Electrical in monitor."),
            OPT_BOOLEAN('\0', "t-memory", &tview_memory, "Toggle view Memory in monitor."),
            OPT_BOOLEAN('\0', "t-gfx", &tview_gfx, "Toggle view GFX in monitor."),
            OPT_BOOLEAN('\0', "t-power", &tview_power, "Toggle view Power in monitor."),
            OPT_GROUP("Get operations"),
            OPT_BOOLEAN('\0', "get-ppt", &get_ppt, "Get PPT Limit (W)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-pptfast", &get_pptfast, "Get PPT Fast Limit (W)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-pptapu", &get_pptapu, "Get PPT APU Limit (W)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-tdc", &get_tdc, "Get TDC Limit (A)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-tdcsoc", &get_tdcsoc, "Get TDC SoC Limit (A)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-edc", &get_edc, "Get EDC Limit (A)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-edcsoc", &get_edcsoc, "Get EDC SoC Limit (A)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-stapm", &get_stapm, "Get STAPM Power Limit (W)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-ppt-time", &get_ppt_time, "Get Slow PPT Time (s)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-stapm-time", &get_stapm_time, "Get STAPM Time (s)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-thm", &get_thm, "Get THM Temperature Limit (degree C)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-scalar", &get_scalar, "Get PBO Scalar", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-ocmode", &get_ocmode, "Get OC Mode", set_cmdmode, 0, 0),
            OPT_STRING('\0', "get-cocount", &get_cocount, "Get CO count for Core, separate with comma for multiple (Starting 0)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "get-cocountall", &get_cocountall, "Get CO count for all for Cores (Starting 0)", set_cmdmode, 0, 0),
            OPT_GROUP("Set operations"),
            OPT_BOOLEAN('\0', "set-enable-oc", &set_enable_oc, "Enable OC mode", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "set-disable-oc", &set_disable_oc, "Disable OC mode", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "set-enable-eco", &set_enable_eco, "Enable Eco Power Saving mode (APU)", set_cmdmode, 0, 0),
            OPT_BOOLEAN('\0', "set-enable-maxperf", &set_enable_maxperf, "Enable Max Performance mode (APU)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-ppt", &set_ppt, "Set PPT Limit (W)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-pptfast", &set_pptfast, "Set PPT Fast Limit (W)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-pptapu", &set_pptapu, "Set PPT APU Limit (W)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-tdc", &set_tdc, "Set TDC Limit (A)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-tdcsoc", &set_tdcsoc, "Set TDC SoC Limit (A)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-edc", &set_edc, "Set EDC Limit (A)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-edcsoc", &set_edcsoc, "Set EDC SoC Limit (A)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-stapm", &set_stapm, "Set STAPM Power Limit (W)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-ppt-time", &set_ppt_time, "Set Slow PPT Time (s)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-stapm-time", &set_stapm_time, "Set STAPM Time (s)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-thm", &set_thm, "Set THM Temperature Limit (degree C)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-scalar", &set_scalar, "Set PBO Scalar", set_cmdmode, 0, 0),
            OPT_STRING('\0', "set-cocount", &set_cocount, "Set CO count for Cores (n+/-30 - syntax: 0+10,1+0,2-20,3-0)", set_cmdmode, 0, 0),
            OPT_INTEGER('\0', "set-cocountall", &set_cocountall, "Set CO count for all Cores (+/-30)", set_cmdmode, 0, 0),
            OPT_END(),
    };
    
    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "\nRyzen Monitor", "\nVersion: v" PROGRAM_VERSION " (NextGeneration - ManniX fork)\n\nNote: set and get operations will override the other options.\nSet and get operations will report NA for Not Available, ERR for SMU/PMT errors and invalid values\n");
    argc = argparse_parse(&argparse, argc, argv);

    ret = smu_init(&obj);
    if (ret != SMU_Return_OK) {
        fprintf(stderr, "Error accessing SMU: %s\n", smu_return_to_str(ret));
        err = -3;
    }

    if (cmd_mode) {

        //cmd_test_hsmp(&pmt, &sysinfo);
        //exit(0);

        if (!err) {
            err = init_pmt(&pmt, forcetable);
            init_sysinfo(&pmt, &sysinfo);
            pmt_refresh(&pmt);            
        }

        if (sysinfo.available && sysinfo.smu_codename != CODENAME_UNDEFINED) {

            if (set_enable_oc) cmd_set_enable_oc(&sysinfo);
            if (set_enable_eco) cmd_set_enable_eco(&sysinfo);
            if (set_enable_maxperf) cmd_set_enable_maxperf(&sysinfo);
            if (set_ppt) cmd_set_ppt(&pmt, &sysinfo, set_ppt);
            if (set_pptfast) cmd_set_pptfast(&pmt, &sysinfo, set_pptfast);
            if (set_pptapu) cmd_set_pptapu(&pmt, &sysinfo, set_pptapu);
            if (set_ppt_time) cmd_set_ppt_time(&pmt, &sysinfo, set_ppt_time);
            if (set_stapm) cmd_set_stapm(&pmt, &sysinfo, set_stapm);
            if (set_stapm_time) cmd_set_stapm_time(&pmt, &sysinfo, set_stapm_time);
            if (set_tdc) cmd_set_tdc(&pmt, &sysinfo, set_tdc);
            if (set_tdcsoc) cmd_set_tdcsoc(&pmt, &sysinfo, set_tdcsoc);
            if (set_edc) cmd_set_edc(&pmt, &sysinfo, set_edc);
            if (set_edcsoc) cmd_set_edcsoc(&pmt, &sysinfo, set_edcsoc);
            if (set_thm) cmd_set_thm(&pmt, &sysinfo, set_thm);
            if (set_scalar) cmd_set_scalar(&sysinfo, set_scalar);
            if (set_cocount) {
                int cocores[sysinfo.cores][2];
                int cc = 0, cx = 0, t = 0;;
                int coreid = -100, corecount = -100;
                int tcore = 0, tcount = 1;
                for (t = 0; t < sysinfo.cores; t++) {
                     cocores[t][tcore] = -1;
                }
                char *delimrest;
                char *delim = strtok_r(set_cocount, ",", &delimrest);
                char *delimpair;
                char *delimcore;
                char *delimcount;
                char *delimsign;
                char* corecountstr;
                while(delim != NULL && cx < sysinfo.cores) {
                    char *delimpairrest;
                    strcpy(delimpair=(char*)malloc(sizeof(delim)),delim);
                    delimsign = strstr(delimpair, "+") ? "+" : strstr(delimpair, "-") ? "-" : "";
                    if (delimsign != "") {
                        delimcore = strtok_r(delimpair, delimsign, &delimpairrest);
                        delimcount = strtok_r(NULL, delimsign, &delimpairrest);
                        if (delimcore != NULL) {
                            coreid = isvalid_coreidstr(delimcore);
                            corecountstr = (char*)malloc(sizeof(delimsign)+sizeof(delimcount));
                            strcat(corecountstr, delimsign);
                            strcat(corecountstr, delimcount);
                            corecount = strtol(corecountstr, NULL, 0);
                            if (coreid >= 0 && strtol(delimcount, NULL, 0)) {
                                corecount = corecount < -30 ? -30 : corecount > +30 ? +30 : corecount;
                                cocores[cc][tcore] = coreid;
                                cocores[cc][tcount] = corecount;
                                cc++;
                            } else {
                                fprintf(stdout, "set-cocount: ERR (%s)(%i)(%i)\n", delim, coreid, corecount);
                            }
                        } 
                    }
                    delim = strtok_r(NULL, ",", &delimrest);
                    cx++;
                }
                free(delimpair);
                free(corecountstr);
                for (t = 0; t < sysinfo.cores; t++) {
                     if (cocores[t][tcore] >= 0) cmd_set_cocount(&sysinfo, cocores[t][tcore], cocores[t][tcount]);
                }
            }
            if (set_cocountall) {
                int count = set_cocountall < -30 ? -30 : set_cocountall > +30 ? +30 : set_cocountall;
                cmd_set_cocountall(&sysinfo, count);
            }
            if (set_disable_oc) cmd_set_disable_oc(&sysinfo);

            if (get_ocmode) cmd_get_ocmode(&sysinfo);
            if (get_ppt) cmd_get_ppt(&pmt);
            if (get_pptfast) cmd_get_pptfast(&pmt);
            if (get_pptapu) cmd_get_pptapu(&pmt);
            if (get_tdc) cmd_get_tdc(&pmt);
            if (get_tdcsoc) cmd_get_tdcsoc(&pmt);
            if (get_edc) cmd_get_edc(&pmt);
            if (get_edcsoc) cmd_get_edcsoc(&pmt);
            if (get_stapm) cmd_get_stapm(&pmt);
            if (get_ppt_time) cmd_get_ppt_time(&pmt);
            if (get_stapm_time) cmd_get_stapm_time(&pmt);
            if (get_thm) cmd_get_thm(&pmt);
            if (get_scalar) cmd_get_scalar(&sysinfo);
            if (get_cocount) {
                int cocores[sysinfo.cores];
                int cc = 0, cx = 0, t = 0;;
                int coreid;
                for (t = 0; t < sysinfo.cores; t++) {
                     cocores[t] = -1;
                }
                char *delim = strtok(get_cocount, ",");
                if (delim != NULL) {
                    while( delim != NULL && cx < sysinfo.cores) {
                        coreid = isvalid_coreidstr(delim);
                        if (coreid >= 0 ) {
                            cocores[cc] = coreid;
                            cc++;
                        } else {
                            fprintf(stdout, "get-cocount: ERR (%s)\n", delim, coreid);
                        }
                        delim = strtok(NULL, ",");
                        cx++;
                    }
                }
                for (t = 0; t < sysinfo.cores; t++) {
                     if (cocores[t] >= 0) cmd_get_cocount(&sysinfo, cocores[t]);
                }
            }
            if (get_cocountall) cmd_get_cocountall(&sysinfo);

        }

    } else {

        if (forcetablestr) {
            errno = 0;
            long val;
            char *leftover;
            if(strncmp(forcetablestr, "0x", 2)){
                char prepend[10];
                strcat(prepend, "0x");
                strcat(prepend, forcetablestr);
                val = strtol(prepend, &leftover, 0);
            } else {
                val = strtol(forcetablestr, &leftover, 0);
            }
            if (leftover == forcetablestr || *leftover != '\0' ||
            ((*leftover == INT_MIN || *leftover == INT_MAX || val <= 0 || val > INT_MAX) && errno == ERANGE)){
                fprintf(stderr, "Wrong forced PM table specified: %u\n", forcetable);
                err = -1;
            }
            forcetable = (unsigned int)val;
        }

        if(dumpfile && !forcetable) {
            fprintf(stderr, "Dumpfile must be used in conjuction with forced PM table switch -f.\n");
            err = -1;
        }

        if (tview_compact) view_compact ^= 1;
        if (tview_info) view_info ^= 1;
        if (tview_counts) view_counts ^= 1;
        if (tview_electrical) view_electrical ^= 1;
        if (tview_memory) view_memory ^= 1;
        if (tview_gfx) view_gfx ^= 1;
        if (tview_power) view_power ^= 1;
        
        if (!err) {
            if(versioninfo)
                print_version();
            else if(dumpfile && !printtimings)
                read_from_dumpfile(dumpfile, forcetable, test_export, dumptable);
            else 
                {
                
                if (getuid() != 0 && geteuid() != 0) {
                    fprintf(stderr, "Program must be run as root.\n");
                    err = -2;
                }

                if (!err) {
                    ret = smu_init(&obj);
                    if (ret != SMU_Return_OK) {
                        fprintf(stderr, "Error accessing SMU: %s\n", smu_return_to_str(ret));
                        err = -3;
                    }
                    if (!err) {
                        if(writedump){
                            err = write_to_dumpfile(writedump);
                        }
                        else if(printtimings) {
                            print_memory_timings();
                        }
                        else {
                            if (force_update_time_s) {
                                update_time_s = force_update_time_s;
                                export_update_time_s = force_update_time_s;
                            }
                            if (!err) {
                                err = init_pmt(&pmt, forcetable);
                                init_sysinfo(&pmt, &sysinfo);
                            }
                            if (!err && pm_export_pipe) {

                                err = start_pm_export();
                            }
                            else if (!err) {
                                start_pm_monitor(forcetable, test_export);
                            }
                        }
                    }
                }
            }
        }
    }

    smu_free(&obj); 

    return err;
}