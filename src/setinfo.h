/**
 * Ryzen SMU Userspace Sensor Monitor and toolset
 *
 * Copyleft ManniX (github.com/mann1x)
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

#ifndef SETINFO_H
#define SETINFO_H

#include "pm_tables.h"
#include "readinfo.h"

void pmt_refresh(pm_table *pmt);

int send_tri_command(unsigned int op_rsmu, unsigned int op_mp1, unsigned int op_hsmp, smu_arg_t *args);

void cmd_get_ppt(pm_table *pmt);
int op_get_ppt(pm_table *pmt);

void cmd_set_ppt(pm_table *pmt, system_info *sysinfo, int val);
int op_set_ppt(system_info *sysinfo, int val);

void cmd_get_pptfast(pm_table *pmt);
int op_get_pptfast(pm_table *pmt);

void cmd_set_pptfast(pm_table *pmt, system_info *sysinfo, int val);
int op_set_pptfast(system_info *sysinfo, int val);

void cmd_get_pptapu(pm_table *pmt);
int op_get_pptapu(pm_table *pmt);

void cmd_set_pptapu(pm_table *pmt, system_info *sysinfo, int val);
int op_set_pptapu(system_info *sysinfo, int val);

void cmd_get_tdc(pm_table *pmt);
int op_get_tdc(pm_table *pmt);

void cmd_set_tdc(pm_table *pmt, system_info *sysinfo, int val);
int op_set_tdc(system_info *sysinfo, int val);

void cmd_get_tdcsoc(pm_table *pmt);
int op_get_tdcsoc(pm_table *pmt);

void cmd_set_tdcsoc(pm_table *pmt, system_info *sysinfo, int val);
int op_set_tdcsoc(system_info *sysinfo, int val);

void cmd_get_edc(pm_table *pmt);
int op_get_edc(pm_table *pmt);

void cmd_set_edc(pm_table *pmt, system_info *sysinfo, int val);
int op_set_edc(system_info *sysinfo, int val);

void cmd_get_edcsoc(pm_table *pmt);
int op_get_edcsoc(pm_table *pmt);

void cmd_set_edcsoc(pm_table *pmt, system_info *sysinfo, int val);
int op_set_edcsoc(system_info *sysinfo, int val);

void cmd_get_stapm(pm_table *pmt);
int op_get_stapm(pm_table *pmt);

void cmd_set_stapm(pm_table *pmt, system_info *sysinfo, int val);
int op_set_stapm(system_info *sysinfo, int val);

void cmd_get_ppt_time(pm_table *pmt);
int op_get_ppt_time(pm_table *pmt);

void cmd_set_ppt_time(pm_table *pmt, system_info *sysinfo, int val);
int op_set_ppt_time(system_info *sysinfo, int val);

void cmd_get_stapm_time(pm_table *pmt);
int op_get_stapm_time(pm_table *pmt);

void cmd_set_stapm_time(pm_table *pmt, system_info *sysinfo, int val);
int op_set_stapm_time(system_info *sysinfo, int val);

void cmd_get_thm(pm_table *pmt);
int op_get_thm(pm_table *pmt);

void cmd_set_thm(pm_table *pmt, system_info *sysinfo, int val);
int op_set_thm(system_info *sysinfo, int val);

void cmd_get_scalar(system_info *sysinfo);
int op_get_scalar(system_info *sysinfo);

void cmd_set_scalar(system_info *sysinfo, int val);
int op_set_scalar(system_info *sysinfo, int val);

void cmd_get_ocmode(system_info *sysinfo);

void cmd_set_enable_oc(system_info *sysinfo);
int op_set_enable_oc(system_info *sysinfo);

void cmd_set_disable_oc(system_info *sysinfo);
int op_set_disable_oc(system_info *sysinfo);

void cmd_get_cocount(system_info *sysinfo, int core);
void cmd_get_cocountall(system_info *sysinfo);
int op_get_cocount(system_info *sysinfo, int val, int use_coremap);

void cmd_set_cocount(system_info *sysinfo, int core, int count);
int op_set_cocount(system_info *sysinfo, int core, int val);

void cmd_set_cocountall(system_info *sysinfo, int count);
int op_set_cocountall(system_info *sysinfo, int val);

void cmd_set_enable_eco(system_info *sysinfo);
int op_set_enable_eco(system_info *sysinfo);

void cmd_set_enable_maxperf(system_info *sysinfo);
int op_set_enable_maxperf(system_info *sysinfo);

#endif
