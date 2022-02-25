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

#include "dslm_ohos_request.h"
#include "dslm_messenger_wrapper.h"
#include "external_interface_adapter.h"

#include <securec.h>
#include <string.h>

#include "utils_hexstring.h"
#include "utils_json.h"
#include "utils_log.h"
#include "utils_mem.h"

#define DSLM_CRED_CFG_FILE_POSITION "/system/etc/dslm_finger.cfg"
#define DSLM_CRED_STR_LEN_MAX 4096
#define CHALLENGE_STRING_LENGTH 32
#define UDID_STRING_LENGTH 65

#define DEVAUTH_JSON_KEY_CHALLENGE "challenge"
#define DEVAUTH_JSON_KEY_PKINFO_LIST "pkInfoList"

static int32_t GetCredFromCurrentDevice(char *credStr, uint32_t maxLen)
{
    FILE *fp = NULL;
    fp = fopen(DSLM_CRED_CFG_FILE_POSITION, "r");
    if (fp == NULL) {
        SECURITY_LOG_ERROR("fopen cred file failed!");
        return ERR_INVALID_PARA;
    }
    int32_t ret = fscanf_s(fp, "%s", credStr, maxLen);
    if (ret == -1) {
        SECURITY_LOG_ERROR("fscanf_s cred file failed!");
        ret = ERR_INVALID_PARA;
    } else {
        ret = SUCCESS;
    }
    if (fclose(fp) != 0) {
        SECURITY_LOG_ERROR("fclose cred file failed!");
        ret = ERR_INVALID_PARA;
    }
    return ret;
}

static int32_t TransToJsonStr(const char *challengeStr, const char *pkInfoListStr, char **nounceStr)
{
    JsonHandle json = CreateJson(NULL);
    if (json == NULL) {
        return ERR_INVALID_PARA;
    }

    // add challenge
    AddFieldStringToJson(json, DEVAUTH_JSON_KEY_CHALLENGE, challengeStr);

    // add pkInfoList
    AddFieldStringToJson(json, DEVAUTH_JSON_KEY_PKINFO_LIST, pkInfoListStr);

    // tran to json
    *nounceStr = (char *)ConvertJsonToString(json);
    if (*nounceStr == NULL) {
        DestroyJson(json);
        return ERR_JSON_ERR;
    }
    DestroyJson(json);
    return SUCCESS;
}

static int32_t GenerateDslmCertChain(const DeviceIdentify *device, const RequestObject *obj, char *credStr,
    uint8_t **certChain, uint32_t *certChainLen)
{
    char *pkInfoListStr = NULL;
    char *nounceStr = NULL;
    char challengeStr[CHALLENGE_STRING_LENGTH] = {0};
    ByteToHexString((uint8_t *)&(obj->challenge), sizeof(obj->challenge), (uint8_t *)challengeStr,
        CHALLENGE_STRING_LENGTH);
    char udidStr[UDID_STRING_LENGTH] = {0};
    if (memcpy_s(udidStr, UDID_STRING_LENGTH, device->identity, device->length) != EOK) {
        return ERR_MEMORY_ERR;
    }
    int32_t ret = ERR_DEFAULT;
    do {
        ret = GetPkInfoListStr(true, udidStr, &pkInfoListStr);
        if (ret != SUCCESS) {
            SECURITY_LOG_ERROR("GetPkInfoListStr failed");
            break;
        }

        ret = TransToJsonStr(challengeStr, pkInfoListStr, &nounceStr);
        if (ret != SUCCESS) {
            SECURITY_LOG_ERROR("TransToJsonStr failed");
            break;
        }
        struct DslmInfoInCertChain saveInfo = {.credStr = credStr, .nounceStr = nounceStr, .udidStr = udidStr};
        ret = DslmCredAttestAdapter(&saveInfo, certChain, certChainLen);
        if (ret != SUCCESS) {
            SECURITY_LOG_ERROR("DslmCredAttestAdapter failed");
            break;
        }
    } while (0);

    if (pkInfoListStr != NULL) {
        FREE(pkInfoListStr);
    }
    if (nounceStr != NULL) {
        FREE(nounceStr);
    }
    return ret;
}

static int32_t SelectDslmCredType(const DeviceIdentify *device, const RequestObject *obj, uint32_t *type)
{
    uint32_t devType = 0;
    const DeviceIdentify *deviceSelf = GetSelfDevice(&devType);
    if (deviceSelf->length == 0) {
        SECURITY_LOG_ERROR("SelectDslmCredType, GetSelfDevice failed");
        return ERR_INVALID_PARA;
    }

    // is self
    if (memcmp(device->identity, deviceSelf->identity, deviceSelf->length) == 0) {
        *type = CRED_TYPE_SMALL;
        return SUCCESS;
    }

    if (HksAttestIsReadyAdapter() != SUCCESS) {
        *type = CRED_TYPE_SMALL;
        return SUCCESS;
    }
    *type = CRED_TYPE_STANDARD;
    return SUCCESS;
}

static int32_t RequestSmallDslmCred(uint8_t *data, uint32_t dataLen, DslmCredBuff **credBuff)
{
    DslmCredBuff *out = CreateDslmCred(CRED_TYPE_SMALL, dataLen, data);
    if (out == NULL) {
        SECURITY_LOG_ERROR("RequestSmallDslmCred, CreateDslmCred failed");
        return ERR_MEMORY_ERR;
    }
    *credBuff = out;
    SECURITY_LOG_INFO("RequestSmallDslmCred success!");
    return SUCCESS;
}

static int32_t RequestStandardDslmCred(const DeviceIdentify *device, const RequestObject *obj, char *credStr,
    DslmCredBuff **credBuff)
{
    uint8_t *certChain = NULL; // malloc, need free
    uint32_t certChainLen = 0;
    int32_t ret = GenerateDslmCertChain(device, obj, credStr, &certChain, &certChainLen);
    if (ret != SUCCESS) {
        SECURITY_LOG_ERROR("RequestStandardDslmCred, GenerateDslmCertChain failed");
        return ret;
    }
    DslmCredBuff *out = CreateDslmCred(CRED_TYPE_STANDARD, certChainLen, certChain);
    if (out == NULL) {
        SECURITY_LOG_ERROR("RequestSmallDslmCred, CreateDslmCred failed");
        return ERR_MEMORY_ERR;
    }
    *credBuff = out;
    SECURITY_LOG_INFO("RequestSmallDslmCred success!");
    return SUCCESS;
}

int32_t RequestOhosDslmCred(const DeviceIdentify *device, const RequestObject *obj, DslmCredBuff **credBuff)
{
    SECURITY_LOG_INFO("Invoke RequestOhosDslmCred");
    uint32_t credType = 0;
    char credStr[DSLM_CRED_STR_LEN_MAX] = {0};
    int32_t ret = GetCredFromCurrentDevice(credStr, DSLM_CRED_STR_LEN_MAX);
    if (ret != SUCCESS) {
        SECURITY_LOG_ERROR("Read cred data from file failed!");
        return ret;
    }

    ret = SelectDslmCredType(device, obj, &credType);
    if (ret != SUCCESS) {
        SECURITY_LOG_ERROR("SelectDslmCredType failed!");
        return ret;
    }
    switch (credType) {
        case CRED_TYPE_SMALL:
            return RequestSmallDslmCred((uint8_t *)credStr, strlen(credStr), credBuff);
        case CRED_TYPE_STANDARD:
            return RequestStandardDslmCred(device, obj, credStr, credBuff);
        default:
            SECURITY_LOG_ERROR("Invalid cred type!");
            break;
    }

    return SUCCESS;
}
