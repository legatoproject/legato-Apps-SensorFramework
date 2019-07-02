//--------------------------------------------------------------------------------------------------
/** @file dm.c
 *
 * This component implements the device management callbacks and registers them to the sensor
 * framework.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include <lwm2mcore/lwm2mcore.h>
#include <lwm2mcore/connectivity.h>
#include <lwm2mcore/device.h>
#include <lwm2mcore/location.h>
#include "interfaces.h"
#include "sensorFw.h"

//--------------------------------------------------------------------------------------------------
/**
 * Maximum allowed size of json string.
 */
//--------------------------------------------------------------------------------------------------
#define MAX_JSON_SIZE 1024


//--------------------------------------------------------------------------------------------------
/**
 * Gpio used to exit from shutdown/ultralow-power state.
 */
//--------------------------------------------------------------------------------------------------
#define WAKEUP_GPIO_NUM    38


//--------------------------------------------------------------------------------------------------
/**
 * lwm2mcore function prototype to read a string
 */
//--------------------------------------------------------------------------------------------------
typedef lwm2mcore_Sid_t (*pf_lwm2mReadString) (char*, size_t*);


//--------------------------------------------------------------------------------------------------
/**
 * Structure for device management data
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char* path;                         ///< [IN] Sensor Path
    bool isReadOnce;                    ///< [IN] Is static resource read once? (e.g serial number)
    char* unit;                         ///< [IN] Unit of measurement
    pfString readConfig;                ///< [IN] Callback function to read configuration
    sensorfwDataType_t type;            ///< [IN] Datatype returned by callback
    void* sampleFunction;               ///< [IN] Callback to sample the sensor
}
dmHandlers_t;


//--------------------------------------------------------------------------------------------------
/**
 * Get JSON document describing the sensor/actuator.
 *
 * @return:
 *         - Json string
  */
//--------------------------------------------------------------------------------------------------
static char* GetJsonDocument
(
    char* name,                        ///< [IN] Sensor name
    char* path,                        ///< [IN] Relative path of sensor in dataHub
    bool isReadOnce,                   ///< [IN] Is sensor read once only?
    char* unit                         ///< [IN] Measurement unit
)
{
    static char ReadStringBuffer[1024];

    const char *isReadOnceString = isReadOnce ? "true" : "false";

    snprintf(ReadStringBuffer,
            sizeof(ReadStringBuffer),
            "{"
            "\"name\" : \"%s\","
            "\"path\" : \"%s\","
            "\"readOnce\" : %s,"
            "\"unit\" : \"%s\""
            "}",
            name,
            path,
            isReadOnceString,
            unit);

    LE_INFO("jsonDoc = %s", ReadStringBuffer);

    return ReadStringBuffer;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read device management data
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ReadDmData
(
    void* lwm2mReadFunction,            ///< [IN] lwm2mcore function to read string
    char* bufferPtr,                    ///< [OUT] Buffer
    size_t* lengthPtr                   ///< [IN] length of buffer
)
{
    pf_lwm2mReadString lwm2mcore_readString = (pf_lwm2mReadString)lwm2mReadFunction;

    if (lwm2mcore_readString(bufferPtr, lengthPtr) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        return LE_OK;
    }
    else
    {
        LE_ERROR("Error reading string from lwm2mcore");
        return LE_FAULT;
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Read serial number of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetSerialNumber
(
    char* serialNumberPtr,                      ///< [OUT] Serial number
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetDeviceSerialNumber, serialNumberPtr, lengthPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read imei of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetImei
(
    char* imeiPtr,                              ///< [OUT] imei
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetDeviceImei, imeiPtr, lengthPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Read iccid of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetIccid
(
    char* iccidPtr,                             ///< [OUT] iccid
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetIccid, iccidPtr, lengthPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Read model number of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetModelNumber
(
    char* modelNumberPtr,                       ///< [OUT] model number
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetDeviceModelNumber, modelNumberPtr, lengthPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read version of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetVersion
(
    char* versionPtr,                           ///< [OUT] model number
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetDeviceFirmwareVersion, versionPtr, lengthPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read reset cause from device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetResetInfo
(
    char* resetPtr,                             ///< [OUT] reset info
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    le_info_Reset_t resetInformation = LE_INFO_RESET_UNKNOWN;

    if (le_info_GetResetInformation(&resetInformation, resetPtr, *lengthPtr) == LE_OK)
    {
        return LE_OK;
    }

    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read timezone
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetTimezone
(
    char* tzStringPtr,                          ///< [OUT] time zone
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    time_t t = time(NULL);
    struct tm lt = {0};

    localtime_r(&t, &lt);

    LE_INFO("The time zone is '%s", lt.tm_zone);

    le_result_t result = le_utf8_Copy(tzStringPtr, lt.tm_zone, *lengthPtr, NULL);
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Read temperature of device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetTemperature
(
    double* tempPtr,                            ///< [OUT] device temperature
    void* contextPtr                            ///< [IN] context
)
{
    int32_t deviceTemp;
    if (lwm2mcore_GetDeviceTemperature(&deviceTemp) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *tempPtr = (double)deviceTemp;
        return LE_OK;
    }

    LE_ERROR("Error reading temperature");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read time from device
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetTime
(
    char* timePtr,                              ///< [OUT] time
    size_t* lengthPtr,                          ///< [INOUT] length of the string
    void* contextPtr                            ///< [IN] context
)
{
    if (le_clk_GetUTCDateTimeString(LE_CLK_STRING_FORMAT_DATE_TIME, timePtr, *lengthPtr , NULL) == LE_OK)
    {
        return LE_OK;
    }

    LE_ERROR("Error reading time");
       return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read signal strength
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetSignalStrength
(
    double* ssPtr,                                  ///< [OUT] signal strength
    void* contextPtr                                ///< [IN] context
)
{
    int32_t signalStrength;
    if (lwm2mcore_GetSignalStrength(&signalStrength) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *ssPtr = (double)signalStrength;
        return LE_OK;
    }

    LE_ERROR("Error reading signal strength");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the network bearer in string format
 *
 * @return:
 *      String
 */
//--------------------------------------------------------------------------------------------------
static char* ConvertBearerToString
(
    lwm2mcore_networkBearer_enum_t nwBearer
)
{
    char *bearerStringPtr;

    switch (nwBearer)
    {
        case LWM2MCORE_NETWORK_BEARER_GSM:
            bearerStringPtr = "GSM";
            break;
        case LWM2MCORE_NETWORK_BEARER_TD_SCDMA:
            bearerStringPtr = "TD-SCDMA";
            break;
        case LWM2MCORE_NETWORK_BEARER_WCDMA:
            bearerStringPtr = "WCDMA";
            break;
        case LWM2MCORE_NETWORK_BEARER_CDMA2000:
            bearerStringPtr = "CDMA2000";
            break;
        case LWM2MCORE_NETWORK_BEARER_WIMAX:
            bearerStringPtr = "WIMAX";
            break;
        case LWM2MCORE_NETWORK_BEARER_LTE_TDD:
            bearerStringPtr = "LTE-TDD";
            break;
        case LWM2MCORE_NETWORK_BEARER_LTE_FDD:
            bearerStringPtr = "LTE-FDD";
            break;
        case LWM2MCORE_NETWORK_BEARER_WLAN:
            bearerStringPtr = "WLAN";
            break;
        case LWM2MCORE_NETWORK_BEARER_BLUETOOTH:
            bearerStringPtr = "Bluetooth";
            break;
        case LWM2MCORE_NETWORK_BEARER_IEEE_802_15_4:
            bearerStringPtr = "IEEE-802.15.4";
            break;
        case LWM2MCORE_NETWORK_BEARER_ETHERNET:
            bearerStringPtr = "Ethernet";
            break;
        case LWM2MCORE_NETWORK_BEARER_DSL:
            bearerStringPtr = "DSL";
            break;
        case LWM2MCORE_NETWORK_BEARER_PLC:
            bearerStringPtr = "PLC";
            break;
        default:
            bearerStringPtr = "Unknown";
            break;
    }

    return bearerStringPtr;
}

//--------------------------------------------------------------------------------------------------
/**
 * Read network bearer
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetBearer
(
    char* bearerPtr,                            ///< [OUT] Get network bearer
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    lwm2mcore_networkBearer_enum_t nwBearer;

    if (lwm2mcore_GetNetworkBearer(&nwBearer) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        le_result_t result = le_utf8_Copy(bearerPtr, ConvertBearerToString(nwBearer), *lengthPtr, NULL);
        return result;
    }

    LE_ERROR("Error reading bearer");
       return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get roaming indicator
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetRoamingIndicator
(
    bool* isRoamingPtr,                         ///< [OUT] 1 = Roaming, 0 = Home
    size_t* lengthPtr,                          ///< [INOUT] length
    void* contextPtr                            ///< [IN] context
)
{
    uint8_t isRoaming = 0;

    if (lwm2mcore_GetRoamingIndicator(&isRoaming) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *isRoamingPtr = isRoaming? true : false;
        return LE_OK;
    }

    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current MCC (Mobile Country Code)
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetMcc
(
    double* mccPtr,                             ///< [OUT] MCC
    void* contextPtr                            ///< [IN] context
)
{
    uint16_t countryCode;
    if (lwm2mcore_GetMncMcc(NULL, &countryCode) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *mccPtr = (double)countryCode;
        return LE_OK;
    }

    LE_ERROR("Error reading MCC");
    return LE_FAULT;
}

//--------------------------------------------------------------------------------------------------
/**
 * Read current MNC (Mobile Network Code)
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetMnc
(
    double* mncPtr,                             ///< [OUT] MNC
    void* contextPtr                            ///< [IN] context
)
{
    uint16_t networkCode;
    if (lwm2mcore_GetMncMcc(&networkCode, NULL) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *mncPtr = (double)networkCode;
        return LE_OK;
    }

    LE_ERROR("Error reading MNC");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current serving cell id
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetCellId
(
    double* retValPtr,                              ///< [OUT] return value
    void* contextPtr                                ///< [IN] context
)
{
    uint32_t cellId;
    if (lwm2mcore_GetCellId(&cellId) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *retValPtr = (double)cellId;
        return LE_OK;
    }

    LE_ERROR("Error reading cellid");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current latitude from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetLatitude
(
    char* latitudePtr,                                  ///< [OUT] Latitude
    size_t* lengthPtr,                                  ///< [INOUT] length
    void* contextPtr                                    ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetLatitude, latitudePtr, lengthPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current longitude from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetLongitude
(
    char* longitudePtr,                             ///< [OUT] Longitude
    size_t* lengthPtr,                              ///< [INOUT] length
    void* contextPtr                                ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetLongitude, longitudePtr, lengthPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Read current altitude from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetAltitude
(
    char* altitudePtr,                              ///< [OUT] Altitude
    size_t* lengthPtr,                              ///< [INOUT] length
    void* contextPtr                                ///< [IN] context
)
{
    return ReadDmData(lwm2mcore_GetAltitude, altitudePtr, lengthPtr);
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current direction from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetDirection
(
    double* retValPtr,                                  ///< [OUT] Direction
    void* contextPtr                                    ///< [IN] context
)
{
    uint32_t direction;
    if (lwm2mcore_GetDirection(&direction) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *retValPtr = (double)direction;
        return LE_OK;
    }

    LE_ERROR("Error reading direction");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current horizontal speed from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetHorizontalSpeed
(
    double* retValPtr,                                  ///< [OUT] Horizontal Speed
    void* contextPtr                                    ///< [IN] context
)
{
    uint32_t hSpeed;
    if (lwm2mcore_GetHorizontalSpeed(&hSpeed) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *retValPtr = (double)hSpeed;
        return LE_OK;
    }

    LE_ERROR("Error reading horizontal speed");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current vertical speed from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetVerticalSpeed
(
    double* retValPtr,                                  ///< [OUT] Vertical Speed
    void* contextPtr                                    ///< [IN] context
)
{
    int32_t vSpeed;
    if (lwm2mcore_GetVerticalSpeed(&vSpeed) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *retValPtr = (double)vSpeed;
        return LE_OK;
    }

    LE_ERROR("Error reading vertical speed");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read current location timestamp from position sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetLocationTimeStamp
(
    double* retValPtr,                                  ///< [OUT] Location timestamp
    void* contextPtr                                    ///< [IN] context
)
{
    uint64_t locTimeStamp;
    if (lwm2mcore_GetLocationTimestamp(&locTimeStamp) == LWM2MCORE_ERR_COMPLETED_OK)
    {
        *retValPtr = (double)locTimeStamp;
        return LE_OK;
    }

    LE_ERROR("Error reading location time stamp");
    return LE_FAULT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read Get Boot Reason
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t GetBootReason
(
    char* retValPtr,                        ///< [OUT] Boot reason
    size_t* length,                         ///< [INOUT] length
    void* contextPtr                        ///< [IN] context
)
{
    char* bootReasonPtr;

    if (le_bootReason_WasTimer())
    {
        bootReasonPtr = "Timer";
    }
    else if (le_bootReason_WasAdc(2))
    {
        bootReasonPtr = "ADC2";
    }
    else if (le_bootReason_WasAdc(3))
    {
        bootReasonPtr = "ADC3";
    }
    else if (le_bootReason_WasGpio(WAKEUP_GPIO_NUM))
    {
        bootReasonPtr = "GPIO";
    }
    else
    {
        bootReasonPtr = "UNKNOWN";
    }

    le_result_t result = le_utf8_Copy(retValPtr, bootReasonPtr, *length, NULL);

    return result;
}


static const dmHandlers_t DmHandlers[] =
{
    // path                    readOnce    unit        config    DataType             sampleFunction
    {"device/SN",              true,        "",        NULL,     SF_CB_STRING,        GetSerialNumber},
    {"device/imei",            true,        "",        NULL,     SF_CB_STRING,        GetImei},
    {"device/iccid",           true,        "",        NULL,     SF_CB_STRING,        GetIccid},
    {"device/model",           true,        "",        NULL,     SF_CB_STRING,        GetModelNumber},
    {"device/version",         true,        "",        NULL,     SF_CB_STRING,        GetVersion},
    {"device/temperature",     false,       "deg C",   NULL,     SF_CB_NUMERIC,       GetTemperature},
    {"device/resetInfo",       true,        "",        NULL,     SF_CB_STRING,        GetResetInfo},
    {"device/time",            false,       "",        NULL,     SF_CB_STRING,        GetTime},
    {"device/tz",              true,        "",        NULL,     SF_CB_STRING,        GetTimezone},
    {"cell/SS",                false,       "dB",      NULL,     SF_CB_NUMERIC,       GetSignalStrength},
    {"cell/bearer",            false,       "",        NULL,     SF_CB_STRING,        GetBearer},
    {"cell/mcc",               false,       "",        NULL,     SF_CB_NUMERIC,       GetMcc},
    {"cell/mnc",               false,       "",        NULL,     SF_CB_NUMERIC,       GetMnc},
    {"cell/cellId",            false,       "",        NULL,     SF_CB_NUMERIC,       GetCellId},
    {"cell/isRoaming",         false,       "",        NULL,     SF_CB_BOOLEAN,       GetRoamingIndicator},
    {"position/latitude",      false,       "Deg",     NULL,     SF_CB_STRING,        GetLatitude},
    {"position/longitude",     false,       "Deg",     NULL,     SF_CB_STRING,        GetLongitude},
    {"position/altitude",      false,       "m",       NULL,     SF_CB_STRING,        GetAltitude},
    {"position/direction",     false,       "Deg",     NULL,     SF_CB_NUMERIC,       GetDirection},
    {"position/hSpeed",        false,       "m/s",     NULL,     SF_CB_NUMERIC,       GetHorizontalSpeed},
    {"position/vSpeed",        false,       "m/s",     NULL,     SF_CB_NUMERIC,       GetVerticalSpeed},
    {"position/timeStamp",     false,       "s",       NULL,     SF_CB_NUMERIC,       GetLocationTimeStamp},
    {"ulpm/bootReason",        true,        "",        NULL,     SF_CB_STRING,        GetBootReason}
};

COMPONENT_INIT
{
    char* jsonDocPtr;
    int i;
    le_result_t result;
    sensorfwCallbacks_t pluginCb;

    LE_INFO("Start DM plugin");

    for (i = 0; i < NUM_ARRAY_MEMBERS(DmHandlers); i++)
    {
        memset(&pluginCb, 0, sizeof(sensorfwCallbacks_t));

        LE_INFO("Register %s", DmHandlers[i].path);
        jsonDocPtr = GetJsonDocument("",
                                     DmHandlers[i].path,
                                     DmHandlers[i].isReadOnce,
                                     DmHandlers[i].unit);

        // Register handlers
        switch (DmHandlers[i].type)
        {
            case SF_CB_BOOLEAN:
                pluginCb.sample.boolCb = (pfBool)DmHandlers[i].sampleFunction;
                break;
            case SF_CB_NUMERIC:
                pluginCb.sample.numericCb = (pfNumeric)DmHandlers[i].sampleFunction;
                break;
            case SF_CB_STRING:
                pluginCb.sample.stringCb = (pfString)DmHandlers[i].sampleFunction;
                break;
            case SF_CB_JSON:
                pluginCb.sample.jsonCb = (pfJSON)DmHandlers[i].sampleFunction;
                break;
            default:
                break;
        }

        result = sensorFw_RegisterCallback(jsonDocPtr,
                                           DmHandlers[i].type,
                                           &pluginCb,
                                           NULL,
                                           NULL);

        if (result != LE_OK)
        {
            LE_ERROR("Registering sensor callback failed");
        }
    }
}
