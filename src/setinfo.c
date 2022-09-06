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

#include <stdlib.h>
#include <stdio.h>
#include <cpuid.h>
#include <ctype.h>
#include <libsmu.h>
#include <math.h>
#include <limits.h>
#include "commonfuncs.h"
#include "pm_tables.h"
#include "readinfo.h"
#include "setinfo.h"

extern smu_obj_t obj;
extern int debuglog;

#define SEND_CMD_RSMU(op) { if (smu_send_command(&obj, op, &args, TYPE_RSMU) != SMU_Return_OK) goto _SEND_ERROR; }
#define SEND_CMD_MP1(op) { if (smu_send_command(&obj, op, &args, TYPE_MP1) != SMU_Return_OK) goto _SEND_ERROR; }
#define SEND_CMD_HSMP(op) { if (smu_send_command(&obj, op, &args, TYPE_HSMP) != SMU_Return_OK) goto _SEND_ERROR; }

#define pmta(elem) ((pmt->elem)?(*pmt->elem):NAN)
//Same, but with 0 as return. For summations that should not fail if one value is not present.
#define pmta0(elem) ((pmt->elem)?(*pmt->elem):0)

const int smu_sleep = 25;
const int smu_sleep_pmt = 100;

const int TEST_INT = 8191;

void pmt_refresh(pm_table *pmt) {
    if (smu_pm_tables_supported(&obj)) {
        unsigned char* pm_buf;
        pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
        select_pm_table_version(obj.pm_table_version, pmt, pm_buf);
        smu_read_pm_table(&obj, pm_buf, obj.pm_table_size);
        msleep(smu_sleep_pmt);
    }
}

int send_tri_command(unsigned int op_rsmu, unsigned int op_mp1, unsigned int op_hsmp, smu_arg_t *args) {
    int ret_rsmu = 1, ret_mp1 = 1, ret_hsmp = 1;
    smu_return_val ret_smu;

    if (op_rsmu != 0x0) {
        ret_smu = smu_send_command(&obj, op_rsmu, args, TYPE_RSMU);
        if ( ret_smu != SMU_Return_OK) {
            if (debuglog)
                fprintf(stderr, "\nSMU Error, RSMU cmd:0x%X MSG=%s\n", op_rsmu, smu_return_to_str(ret_smu));
        } else {
            ret_rsmu = 0;
        }
        msleep(smu_sleep);
    }

    if (op_mp1 != 0x0) {
        ret_smu = smu_send_command(&obj, op_mp1, args, TYPE_MP1);
        if ( ret_smu != SMU_Return_OK) {
            if (debuglog)
                fprintf(stderr, "\nSMU Error, MP1 cmd:0x%X MSG=%s\n", op_mp1, smu_return_to_str(ret_smu));
        } else {
            ret_mp1 = 0;        
        }
        msleep(smu_sleep);
    }

    if (op_hsmp != 0x0) {
        ret_smu = smu_send_command(&obj, op_hsmp, args, TYPE_HSMP);
        if ( ret_smu != SMU_Return_OK) {
            if (debuglog)
                fprintf(stderr, "\nSMU Error, HSMP cmd:0x%X MSG=%s\n", op_hsmp, smu_return_to_str(ret_smu));
        } else {
            ret_hsmp = 0;        
        }
        msleep(smu_sleep);
    }

    if (ret_rsmu == 0 || ret_mp1 == 0 || ret_hsmp == 0)
        return 0;
    return 1;

}

void cmd_get_ppt(pm_table *pmt) {
    if (pmta(PPT_LIMIT) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-ppt: %i\n", op_get_ppt(pmt));
    } else {
        fprintf(stdout, "get-ppt: NA\n");
    }
}

int op_get_ppt(pm_table *pmt) {
    return pmta0(PPT_LIMIT);
}

void cmd_set_ppt(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-ppt: ");
    int before = -100;
    int after = -100;
    if (pmta(PPT_LIMIT) != NAN && pmt->zen_version) {
        before = op_get_ppt(pmt);
    }
    int ret = op_set_ppt(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(PPT_LIMIT) != NAN && pmt->zen_version) {
        after = op_get_ppt(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_ppt(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x53;
            op_mp1 = 0x3D;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1C;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0x33;
            op_mp1 = 0x16;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }

    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
}

void cmd_get_pptfast(pm_table *pmt) {
    if (pmta(PPT_LIMIT_FAST) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-pptfast: %i\n", op_get_pptfast(pmt));
    } else {
        fprintf(stdout, "get-pptfast: NA\n");
    }
}

int op_get_pptfast(pm_table *pmt) {
    return pmta0(PPT_LIMIT_FAST);
}

void cmd_set_pptfast(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-pptfast: ");
    int before = -100;
    int after = -100;
    if (pmta(PPT_LIMIT_FAST) != NAN && pmt->zen_version) {
        before = op_get_pptfast(pmt);
    }
    int ret = op_set_pptfast(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(PPT_LIMIT_FAST) != NAN && pmt->zen_version) {
        after = op_get_pptfast(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_pptfast(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1B;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_mp1 = 0x15;
            break;
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
}

void cmd_get_pptapu(pm_table *pmt) {
    if (pmta(PPT_LIMIT_APU) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-pptapu: %i\n", op_get_pptapu(pmt));
    } else {
        fprintf(stdout, "get-pptapu: NA\n");
    }
}

int op_get_pptapu(pm_table *pmt) {
    return pmta0(PPT_LIMIT_APU);
}

void cmd_set_pptapu(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-pptapu: ");
    int before = -100;
    int after = -100;
    if (pmta(PPT_LIMIT_APU) != NAN && pmt->zen_version) {
        before = op_get_pptapu(pmt);
    }
    int ret = op_set_pptapu(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(PPT_LIMIT_APU) != NAN && pmt->zen_version) {
        after = op_get_pptapu(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_pptapu(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x23;
            break;
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_mp1 = 0x21;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
}

void cmd_get_tdc(pm_table *pmt) {
    if (pmta(TDC_LIMIT) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-tdc: %i\n", op_get_tdc(pmt));
    } else {
        fprintf(stdout, "get-tdc: NA\n");
    }
}

int op_get_tdc(pm_table *pmt) {
    return pmta0(TDC_LIMIT);
}

void cmd_set_tdc(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-tdc: ");
    int before = -100;
    int after = -100;
    if (pmta(TDC_LIMIT) != NAN && pmt->zen_version) {
        before = op_get_tdc(pmt);
    }
    int ret = op_set_tdc(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(TDC_LIMIT) != NAN && pmt->zen_version) {
        after = op_get_tdc(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_tdc(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x54;
            op_mp1 = 0x3B;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x20;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            //op_rsmu = 0x38;
            op_mp1 = 0x1A;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x6B;
            break;
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_tdcsoc(pm_table *pmt) {
    if (pmta(TDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-tdcsoc: %i\n", op_get_tdcsoc(pmt));
    } else {
        fprintf(stdout, "get-tdcsoc: NA\n");
    }
}

int op_get_tdcsoc(pm_table *pmt) {
    return pmta0(TDC_LIMIT_SOC);
}

void cmd_set_tdcsoc(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-tdcsoc: ");
    int before = -100;
    int after = -100;
    if (pmta(TDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        before = op_get_tdcsoc(pmt);
    }
    int ret = op_set_tdcsoc(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(TDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        after = op_get_tdcsoc(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_tdcsoc(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x21;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0x39;
            op_mp1 = 0x1B;
            break;
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_edc(pm_table *pmt) {
    if (pmta(EDC_LIMIT) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-edc: %i\n", op_get_edc(pmt));
    } else {
        fprintf(stdout, "get-edc: NA\n");
    }
}

int op_get_edc(pm_table *pmt) {
    return pmta0(EDC_LIMIT);
}

void cmd_set_edc(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-edc: ");
    int before = -100;
    int after = -100;
    if (pmta(EDC_LIMIT) != NAN && pmt->zen_version) {
        before = op_get_edc(pmt);
    }
    int ret = op_set_edc(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(EDC_LIMIT) != NAN && pmt->zen_version) {
        after = op_get_edc(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_edc(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x55;
            op_mp1 = 0x3C;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x20;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0x3A;
            op_mp1 = 0x1c;
            break;
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x1e;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
            op_rsmu = 0x66;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x6C;
            break;
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_edcsoc(pm_table *pmt) {
    if (pmta(EDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-edcsoc: %i\n", op_get_edcsoc(pmt));
    } else {
        fprintf(stdout, "get-edcsoc: NA\n");
    }
}

int op_get_edcsoc(pm_table *pmt) {
    return pmta0(EDC_LIMIT_SOC);
}

void cmd_set_edcsoc(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-edcsoc: ");
    int before = -100;
    int after = -100;
    if (pmta(EDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        before = op_get_edcsoc(pmt);
    }
    int ret = op_set_edcsoc(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(EDC_LIMIT_SOC) != NAN && pmt->zen_version) {
        after = op_get_edcsoc(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_edcsoc(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x23;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0x3B;
            op_mp1 = 0x1D;
            break;
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_stapm(pm_table *pmt) {
    if (pmta(STAPM_LIMIT) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-stapm: %i\n", op_get_stapm(pmt));
    } else {
        fprintf(stdout, "get-stapm: NA\n");
    }
}

int op_get_stapm(pm_table *pmt) {
    return pmta0(STAPM_LIMIT);
}

void cmd_set_stapm(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-stapm: ");
    int before = -100;
    int after = -100;
    if (pmta(STAPM_LIMIT) != NAN && pmt->zen_version) {
        before = op_get_stapm(pmt);
    }
    int ret = op_set_stapm(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(STAPM_LIMIT) != NAN && pmt->zen_version) {
        after = op_get_stapm(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_stapm(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*1000;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1A;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_rsmu = 0x31;
            op_mp1 = 0x14;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_ppt_time(pm_table *pmt) {
    if (pmta(SlowPPTTimeConstant) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-ppt-time: %i\n", op_get_ppt_time(pmt));
    } else {
        fprintf(stdout, "get-ppt-time: NA\n");
    }
}

int op_get_ppt_time(pm_table *pmt) {
    return pmta0(SlowPPTTimeConstant);
}

void cmd_set_ppt_time(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-ppt-time: ");
    int before = -100;
    int after = -100;
    if (pmta(SlowPPTTimeConstant) != NAN && pmt->zen_version) {
        before = op_get_ppt_time(pmt);
    }
    int ret = op_set_ppt_time(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(SlowPPTTimeConstant) != NAN && pmt->zen_version) {
        after = op_get_ppt_time(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_ppt_time(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1D;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x17;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_stapm_time(pm_table *pmt) {
    if (pmta(StapmTimeConstant) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-stapm-time: %i\n", op_get_stapm_time(pmt));
    } else {
        fprintf(stdout, "get-stapm-time: NA\n");
    }
}

int op_get_stapm_time(pm_table *pmt) {
    return pmta0(StapmTimeConstant);
}

void cmd_set_stapm_time(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-stapm-time: ");
    int before = -100;
    int after = -100;
    if (pmta(StapmTimeConstant) != NAN && pmt->zen_version) {
        before = op_get_stapm_time(pmt);
    }
    int ret = op_set_stapm_time(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(StapmTimeConstant) != NAN && pmt->zen_version) {
        after = op_get_stapm_time(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_stapm_time(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val;

    switch(sysinfo->smu_codename) {
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1E;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x18;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_thm(pm_table *pmt) {
    if (pmta(THM_LIMIT) != NAN && pmt->zen_version) {
        fprintf(stdout, "get-thm: %i\n", op_get_thm(pmt));
    } else {
        fprintf(stdout, "get-thm: NA\n");
    }
}

int op_get_thm(pm_table *pmt) {
    return pmta0(THM_LIMIT);
}

void cmd_set_thm(pm_table *pmt, system_info *sysinfo, int val) {
    fprintf(stdout, "set-thm: ");
    int before = -100;
    int after = -100;
    if (pmta(THM_LIMIT) != NAN && pmt->zen_version) {
        before = op_get_thm(pmt);
    }
    int ret = op_set_thm(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    pmt_refresh(pmt);
    if (pmta(THM_LIMIT) != NAN && pmt->zen_version) {
        after = op_get_thm(pmt);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_thm(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val;

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x56;
            op_mp1 = 0x3E;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x6E;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x1A;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_rsmu = 0x37;
            op_mp1 = 0x3E;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_scalar(system_info *sysinfo) {
    int scalar = op_get_scalar(sysinfo);
    if (scalar == -100) {
        fprintf(stdout, "get-scalar: NA\n");
    } else if (scalar == -200) {
        fprintf(stdout, "get-scalar: ERROR\n");
    } else {
        fprintf(stdout, "get-scalar: %i\n", scalar);
    }
}

int op_get_scalar(system_info *sysinfo) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    switch(sysinfo->smu_codename) {
        case CODENAME_PINNACLERIDGE: //ZenPSettings
            op_rsmu = 0x6A;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x70;
            break;
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x6C;
            break;
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_rsmu = 0x68;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
            op_rsmu = 0x62;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0xF;
            break;
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_set_scalar(system_info *sysinfo, int val) {
    fprintf(stdout, "set-scalar: ");
    int before = -100;
    int after = -100;
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        before = op_get_scalar(sysinfo);
    }
    int ret = op_set_scalar(sysinfo, val);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "%i", val);
    }
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        after = op_get_scalar(sysinfo);
    }
    if (before > -100) fprintf(stdout, " before: %i", before);
    if (after > -100) fprintf(stdout, " after: %i", after);
    fprintf(stdout, "\n");
}

int op_set_scalar(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));
    args.i.args0 = (u_int32_t)val*100;

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x58;
            op_mp1 = 0x2F;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x6F;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_rsmu = 0x3F;
            op_mp1 = 0x49;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
            op_rsmu = 0x6A;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_ocmode(system_info *sysinfo) {
    int scalar = op_get_scalar(sysinfo);
    if (scalar == -100) {
        fprintf(stdout, "get-ocmode: NA\n");
    } else if (scalar == -200) {
        fprintf(stdout, "get-ocmode: ERROR\n");
    } else if (scalar != 0) {
        fprintf(stdout, "get-ocmode: 0\n");
    } else {
        fprintf(stdout, "get-ocmode: 1\n");
    }
}

void cmd_set_enable_oc(system_info *sysinfo) {
    fprintf(stdout, "set-enable-oc: ");
    int before = -100;
    int after = -100;
    int state = -1;
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        before = op_get_scalar(sysinfo);
    }
    int ret = op_set_enable_oc(sysinfo);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "1");
    }
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        after = op_get_scalar(sysinfo);
    }
    if (before > -100) {
        state = before != 0 ? 0 : 1;
        fprintf(stdout, " before: %i", state);
    }
    if (after > -100) {
        state = after != 0 ? 0 : 1;
        fprintf(stdout, " after: %i", state);
    }
    fprintf(stdout, "\n");
}

int op_set_enable_oc(system_info *sysinfo) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x5A;
            op_mp1 = 0x24;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x63;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_rsmu = 0x17;
            op_mp1 = 0x2F;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
            op_rsmu = 0x6A;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_rsmu = 0x69;
            break;
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
            op_mp1 = 0x23;
            break;
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_set_disable_oc(system_info *sysinfo) {
    fprintf(stdout, "set-disable-oc: ");
    int before = -100;
    int after = -100;
    int state = -1;
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        before = op_get_scalar(sysinfo);
    }
    int ret = op_set_disable_oc(sysinfo);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "0");
    }
    if (op_set_scalar(sysinfo, TEST_INT) != -100) {
        after = op_get_scalar(sysinfo);
    }
    if (before > -100) {
        state = before != 0 ? 0 : 1;
        fprintf(stdout, " before: %i", state);
    }
    if (after > -100) {
        state = after != 0 ? 0 : 1;
        fprintf(stdout, " after: %i", state);
    }
    fprintf(stdout, "\n");
}

int op_set_disable_oc(system_info *sysinfo) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    switch(sysinfo->smu_codename) {
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0x5B;
            op_mp1 = 0x25;
            break;
        case CODENAME_COLFAX: //ColfaxSettings
            op_rsmu = 0x64;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_rsmu = 0x1D;
            op_mp1 = 0x30;
            break;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            //op_rsmu = 0x18;
            op_mp1 = 0x30;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_rsmu = 0x6A;
            break;
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
            op_mp1 = 0x24;
            break;
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }

    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_get_cocount(system_info *sysinfo, int core) {
    int cocount = op_get_cocount(sysinfo, core, 1);
    if (cocount == -100) {
        fprintf(stdout, "get-cocount: NA\n");
    } else if (cocount == -200) {
        fprintf(stdout, "get-cocount: ERROR\n");
    } else {
        fprintf(stdout, "get-cocount: %i%+.1i\n", core, cocount);
    }
}

void cmd_get_cocountall(system_info *sysinfo) {
    int cocount;
    if (op_get_cocount(sysinfo, TEST_INT, 1) == -100) {
        fprintf(stdout, "get-cocountall: NA\n");
    } else {
        int i;
        fprintf(stdout, "get-cocountall: ");
        for (i = 0; i < sysinfo->cores; i++) {
            cocount = op_get_cocount(sysinfo, i, 1);
            if (cocount <= -100) cocount = 0; 
            fprintf(stdout, "%i%+.1i", i, cocount);
            if (i < sysinfo->cores-1) fprintf(stdout, ",");
        }
        fprintf(stdout, "\n");
    }
}

int op_get_cocount(system_info *sysinfo, int val, int use_coremap) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    u_int32_t ccxInCcd = sysinfo->ccxs;
    u_int32_t ccx = sysinfo->ccxs;
    u_int32_t ccd = sysinfo->ccds;
    u_int32_t coresInCcx = 8 / ccxInCcd;
    int core = val;

    if (use_coremap) {
        if (val < 0 || val > (sysinfo->cores)-1) return -200;
        core = sysinfo->coremap[val];
    }

    switch(sysinfo->smu_codename) {
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            args.i.args0 = (u_int32_t)(((ccd << 4 | ccx % ccxInCcd & 0xF) << 4 | core % coresInCcx & 0xF) << 20);
            op_mp1 = 0x48;
            break;
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            args.i.args0 = (u_int32_t)core;
            op_rsmu = 0x2F;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            args.i.args0 = (u_int32_t)core;
            op_rsmu = 0xC3;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return args.i.args0;

}

void cmd_set_cocount(system_info *sysinfo, int core, int count) {
    int cocount = op_set_cocount(sysinfo, core, count);
    if (cocount == -100) {
        fprintf(stdout, "set-cocount: NA\n");
    } else if (cocount == -200) {
        fprintf(stdout, "set-cocount: ERROR\n");
    } else {
        fprintf(stdout, "set-cocount: %i%+.1i\n", core, count);
    }
}

int op_set_cocount(system_info *sysinfo, int coreidx, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    u_int32_t ccxInCcd = sysinfo->ccxs;
    u_int32_t ccx = sysinfo->ccxs;
    u_int32_t ccd = sysinfo->ccds;
    u_int32_t coresInCcx = 8 / ccxInCcd;

    if (coreidx < 0 || coreidx > (sysinfo->cores)-1) return -200;

    int core = sysinfo->coremap[coreidx];

    switch(sysinfo->smu_codename) {
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            //args.i.args0 = (u_int32_t)(((ucore & 8) << 5 | (ucore & 7)) << 20 | (count & 65535));
            args.i.args0 = (u_int32_t)(((ccd << 4 | ccx % ccxInCcd & 0xF) << 4 | core % coresInCcx & 0xF) << 20 | (val & 65535));
            op_mp1 = 0x35;
            op_rsmu = 0xA;
            break;
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            args.i.args0 = (u_int32_t)(core << 20 | (val & 0xFFFF));
            op_mp1 = 0x53;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            args.i.args0 = (u_int32_t)(core << 20 | (val & 0xFFFF));
            op_rsmu = 0x52;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_set_cocountall(system_info *sysinfo, int count) {
    int cocount = op_set_cocountall(sysinfo, count);
    if (cocount == -100) {
        fprintf(stdout, "set-cocountall: NA\n");
    } else if (cocount == -200) {
        fprintf(stdout, "set-cocountall: ERROR\n");
    } else {
        fprintf(stdout, "set-cocountall: %+.0i\n", count);
    }
}

int op_set_cocountall(system_info *sysinfo, int val) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    u_int32_t ccxInCcd = sysinfo->ccxs;
    u_int32_t ccx = sysinfo->ccxs;
    u_int32_t ccd = sysinfo->ccds;
    u_int32_t coresInCcx = 8 / ccxInCcd;
    
    args.i.args0 = (u_int32_t)(val & 65535);

    switch(sysinfo->smu_codename) {
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
            op_rsmu = 0xB;
            op_mp1 = 0x36;
            break;
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
            op_mp1 = 0x4C;
            break;
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x5D;
            break;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
            op_rsmu = 0xB1;
            op_mp1 = 0x55;
            break;
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;
    // this is a check set is supported
    if (val == TEST_INT) return 0;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
}

void cmd_set_enable_eco(system_info *sysinfo) {
    fprintf(stdout, "set-enable-eco: ");
    int ret = op_set_enable_eco(sysinfo);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "1");
    }
    fprintf(stdout, "\n");
}

int op_set_enable_eco(system_info *sysinfo) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    switch(sysinfo->smu_codename) {
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x12;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x19;
            break;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}

void cmd_set_enable_maxperf(system_info *sysinfo) {
    fprintf(stdout, "set-enable-maxperf: ");
    int ret = op_set_enable_maxperf(sysinfo);
    if (ret == -200){
        fprintf(stdout, "ERR");
    } else if (ret <= -100){
        fprintf(stdout, "NA");
    } else {
        fprintf(stdout, "1");
    }
    fprintf(stdout, "\n");
}

int op_set_enable_maxperf(system_info *sysinfo) {
    unsigned int op_rsmu = 0x0;
    unsigned int op_mp1 = 0x0;
    unsigned int op_hsmp = 0x0;
    smu_arg_t args;
    int ret = 0;

    memset(&args, 0, sizeof(args));

    switch(sysinfo->smu_codename) {
        case CODENAME_REMBRANDT: //APUSettings2 -> APUSettings1
        case CODENAME_RENOIR: //APUSettings1
        case CODENAME_LUCIENNE: //APUSettings1
        case CODENAME_VANGOGH: //APUSettings2 -> APUSettings1
            op_mp1 = 0x11;
            break;
        case CODENAME_PICASSO: //APUSettings0_Picasso -> APUSettings0
        case CODENAME_DALI: //APUSettings0
        case CODENAME_RAVENRIDGE: //APUSettings0
        case CODENAME_RAVENRIDGE2: //APUSettings0
            op_mp1 = 0x18;
            break;
        case CODENAME_CEZANNE: //APUSettings1_Cezanne
        case CODENAME_SUMMITRIDGE: //SummitRidgeSettings
        case CODENAME_MATISSE: //Zen2Settings
        case CODENAME_CASTLEPEAK: //Zen2Settings
        case CODENAME_VERMEER: //Zen3Settings -> Zen2Settings
        case CODENAME_MILAN: //Zen3Settings -> Zen2Settings
        case CODENAME_COLFAX: //ColfaxSettings
        case CODENAME_PINNACLERIDGE: //ZenPSettings
        case CODENAME_THREADRIPPER:
        case CODENAME_UNDEFINED:
        default:
            break;
    }
    
    if (op_rsmu == 0x0 && op_mp1 == 0x0 && op_hsmp == 0x0) return -100;

    ret = send_tri_command(op_rsmu, op_mp1, op_hsmp, &args);

    if (ret == 1) return -200;

    return (int)args.f.args0_f;
    
}
