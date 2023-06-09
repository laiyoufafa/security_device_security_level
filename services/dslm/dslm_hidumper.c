/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include "securec.h"

#include "utils_datetime.h"
#include "utils_log.h"

#include "dslm_device_list.h"
#include "dslm_fsm_process.h"
#include "dslm_hidumper.h"

#define SPLIT_LINE "------------------------------------------------------"
#define END_LINE "\n"

#define TIME_STRING_LEN 256

static const char *GetTimeStringFromTimeStamp(uint64_t timeStamp)
{
    static char timeBuff[TIME_STRING_LEN] = {0};
    DateTime dateTime = {0};
    bool success = false;
    do {
        (void)memset_s(timeBuff, TIME_STRING_LEN, 0, TIME_STRING_LEN);
        if (timeStamp == 0) {
            break;
        }
        if (!GetDateTimeByMillisecondSinceBoot(timeStamp, &dateTime)) {
            SECURITY_LOG_ERROR("GetTimeStringFromTimeStamp GetDateTimeByMillisecondSinceBoot error");
            break;
        }
        int ret = snprintf_s(timeBuff, TIME_STRING_LEN, TIME_STRING_LEN - 1, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            dateTime.year, dateTime.mon, dateTime.day, dateTime.hour, dateTime.min, dateTime.sec, dateTime.msec);
        if (ret < 0) {
            break;
        }
        success = true;
    } while (0);

    if (!success) {
        if (snprintf_s(timeBuff, TIME_STRING_LEN, TIME_STRING_LEN - 1, "-") < 0) {
            SECURITY_LOG_ERROR("GetTimeStringFromTimeStamp snprintf_s error");
        }
    }
    return timeBuff;
}

static const char *GetMachineState(const DslmDeviceInfo *info)
{
    uint32_t state = GetCurrentMachineState(info);
    switch (state) {
        case STATE_INIT:
            return "STATE_INIT";
        case STATE_WAITING_CRED_RSP:
            return "STATE_WAITING_CRED_RSP";
        case STATE_SUCCESS:
            return "STATE_SUCCESS";
        case STATE_FAILED:
            return "STATE_FAILED";
        default:
            return "STATE_UNKOWN";
    }
}

static const char *GetCreadType(const DslmDeviceInfo *info)
{
    switch (info->credInfo.credType) {
        case CRED_TYPE_MINI:
            return "mini";
        case CRED_TYPE_SMALL:
            return "small";
        case CRED_TYPE_STANDARD:
            return "standard";
        case CRED_TYPE_LARGE:
            return "large";
        default:
            return "default";
    }
}

static int32_t GetPendingNotifyNodeCnt(const DslmDeviceInfo *info)
{
    int result = 0;
    LockDslmStateMachine((DslmDeviceInfo *)info);
    ListNode *node = NULL;
    FOREACH_LIST_NODE (node, &info->notifyList) {
        result++;
    }
    UnLockDslmStateMachine((DslmDeviceInfo *)info);
    return result;
}

static void PrintBanner(int fd)
{
    dprintf(fd, " ___  ___ _    __  __   ___  _   _ __  __ ___ ___ ___ " END_LINE);
    dprintf(fd, "|   \\/ __| |  |  \\/  | |   \\| | | |  \\/  | _ \\ __| _ \\" END_LINE);
    dprintf(fd, "| |) \\__ \\ |__| |\\/| | | |) | |_| | |\\/| |  _/ __|   /" END_LINE);
    dprintf(fd, "|___/|___/____|_|  |_| |___/ \\___/|_|  |_|_| |___|_|_\\" END_LINE);
}

static void DumpOneDevice(const DslmDeviceInfo *info, int32_t fd)
{
    if (info == NULL) {
        return;
    }

    dprintf(fd, SPLIT_LINE END_LINE);
    dprintf(fd, "DEVICE_ID                 : %x" END_LINE, info->machine.machineId);
    dprintf(fd, "DEVICE_TYPE               : %d" END_LINE, info->deviceType);
    dprintf(fd, END_LINE);

    dprintf(fd, "DEVICE_ONLINE_STATUS      : %s" END_LINE, info->onlineStatus ? "online" : "offline");
    dprintf(fd, "DEVICE_ONLINE_TIME        : %s" END_LINE, GetTimeStringFromTimeStamp(info->lastOnlineTime));
    dprintf(fd, "DEVICE_OFFLINE_TIME       : %s" END_LINE, GetTimeStringFromTimeStamp(info->lastOfflineTime));
    dprintf(fd, "DEVICE_REQUEST_TIME       : %s" END_LINE, GetTimeStringFromTimeStamp(info->lastRequestTime));
    dprintf(fd, "DEVICE_RESPONE_TIME       : %s" END_LINE, GetTimeStringFromTimeStamp(info->lastResponseTime));
    dprintf(fd, END_LINE);

    dprintf(fd, "DEVICE_PENDING_CNT        : %d" END_LINE, GetPendingNotifyNodeCnt(info));
    dprintf(fd, "DEVICE_MACHINE_STATUS     : %s" END_LINE, GetMachineState(info));
    dprintf(fd, "DEVICE_VERIFIED_LEVEL     : %d" END_LINE, info->credInfo.credLevel);
    dprintf(fd, "DEVICE_VERIFIED_RESULT    : %s" END_LINE, info->result == 0 ? "success" : "failed");
    dprintf(fd, END_LINE);

    dprintf(fd, "CRED_TYPE                 : %s" END_LINE, GetCreadType(info));
    dprintf(fd, "CRED_RELEASE_TYPE         : %s" END_LINE, info->credInfo.releaseType);
    dprintf(fd, "CRED_SIGNTIME             : %s" END_LINE, info->credInfo.signTime);
    dprintf(fd, "CRED_MANUFACTURE          : %s" END_LINE, info->credInfo.manufacture);
    dprintf(fd, "CRED_BAND                 : %s" END_LINE, info->credInfo.brand);
    dprintf(fd, "CRED_MODEL                : %s" END_LINE, info->credInfo.model);
    dprintf(fd, "CRED_SOFTWARE_VERSION     : %s" END_LINE, info->credInfo.softwareVersion);
    dprintf(fd, "CRED_SECURITY_LEVEL       : %s" END_LINE, info->credInfo.securityLevel);
    dprintf(fd, "CRED_VERSION              : %s" END_LINE, info->credInfo.version);
    dprintf(fd, SPLIT_LINE END_LINE);
}

static void PrintAllDevices(int fd)
{
    ForEachDeviceDump(DumpOneDevice, fd);
}

void DslmDumper(int fd)
{
    PrintBanner(fd);
    PrintAllDevices(fd);
}