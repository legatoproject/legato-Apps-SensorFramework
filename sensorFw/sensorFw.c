//--------------------------------------------------------------------------------------------------
/** @file sensorFw.c
 *
 * Implementation of the sensor framework. Provides an api to register sensors to the framework
 * and interfaces between the datahub and sensor plugins.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "sensorFw.h"
#include "config.h"

#define dhubIO_DataType_t io_DataType_t

#include "periodicSensor.h"
#include "json.h"

//--------------------------------------------------------------------------------------------------
/**
 * Default period at which a sensor is sampled
 */
//--------------------------------------------------------------------------------------------------
#define     DEFAULT_SAMPLING_PERIOD_SEC          60

//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes for a sensor resource name
 */
//--------------------------------------------------------------------------------------------------
#define     MAX_RESOURCE_NAME_LEN                20

//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes for the string resource
 */
//--------------------------------------------------------------------------------------------------
#define     MAX_RES_STRING_LEN                   1024


//--------------------------------------------------------------------------------------------------
/**
 * Information about a sensor registered to the framework.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    int sensorId;                                ///< sensor index
    char name[MAX_RESOURCE_NAME_LEN];            ///< Name of the sensor
    char path[IO_MAX_RESOURCE_PATH_LEN];         ///< Path name provided by plugin
    bool isReadOnce;                             ///< is sensor to be sampled only once?
    char unit[IO_MAX_UNITS_NAME_LEN];            ///< Measurement unit
    dhubIO_DataType_t type;                      ///< data type of entry in datahub
    psensor_Ref_t sensorRef;                     ///< Reference for the periodic sensor
}
sensorInfo_t;

//--------------------------------------------------------------------------------------------------
/**
 * Sensor handler
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    sensorInfo_t info;                           ///< Information about the registered sensor
    sensorfwCallbacks_t callbacks;               ///< Callbacks implemented by the plugin
    void* pluginContextPtr;                      ///< Context passed by plugin
}
sensorHandler_t;


//--------------------------------------------------------------------------------------------------
/**
 * Pool of registered sensors
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t SensorHandlerPool = NULL;


//--------------------------------------------------------------------------------------------------
/**
 * Define memory pools statically
 */
//--------------------------------------------------------------------------------------------------
LE_MEM_DEFINE_STATIC_POOL(SensorHandlerPool,
    SENSOR_HANDLER_POOL_SIZE, sizeof(sensorHandler_t));

//--------------------------------------------------------------------------------------------------
/**
 * Count for the number of sensors registered to the framework
 */
//--------------------------------------------------------------------------------------------------
static int RegisteredSensorCount;

//--------------------------------------------------------------------------------------------------
/**
 * Samples data and pushes the sample to datahub
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t PushData
(
    sensorHandler_t* handlerPtr                  ///< [IN] Handler to the registered sensor
)
{
    le_result_t result;

    pfBool readBoolValue;
    pfNumeric readNumericValue;
    pfString readStringValue;

    bool boolSample;
    double numericSample;
    char sampleString[MAX_RES_STRING_LEN];
    size_t length;

    switch(handlerPtr->info.type)
    {
        case IO_DATA_TYPE_BOOLEAN:
            readBoolValue = (pfBool)(handlerPtr->callbacks.sample.boolCb);
            length = sizeof(bool);
            result = readBoolValue(&boolSample, &length, handlerPtr->pluginContextPtr);

            if (result == LE_OK)
            {
                if (handlerPtr->info.isReadOnce)
                {
                    io_PushBoolean(handlerPtr->info.path, IO_NOW, boolSample);
                }
                else
                {
                    psensor_PushBoolean(handlerPtr->info.sensorRef, IO_NOW, boolSample);
                }
            }
            else
            {
                LE_ERROR("Error sampling sensor");
                return LE_FAULT;
            }
            break;

        case IO_DATA_TYPE_NUMERIC:
            readNumericValue = (pfNumeric)(handlerPtr->callbacks.sample.numericCb);
            length = sizeof(numericSample);
            result = readNumericValue(&numericSample, &length, handlerPtr->pluginContextPtr);

            if (result == LE_OK)
            {
                if (handlerPtr->info.isReadOnce)
                {
                    io_PushNumeric(handlerPtr->info.path, IO_NOW, numericSample);
                }
                else
                {
                    psensor_PushNumeric(handlerPtr->info.sensorRef, IO_NOW, numericSample);
                }
            }
            else
            {
                LE_ERROR("Error sampling sensor");
                return LE_FAULT;
            }
            break;

        case IO_DATA_TYPE_STRING:
            readStringValue = (pfString)(handlerPtr->callbacks.sample.stringCb);
            length = sizeof(sampleString);
            result = readStringValue(sampleString, &length, handlerPtr->pluginContextPtr);

            if (result == LE_OK)
            {
                if (handlerPtr->info.isReadOnce)
                {
                    io_PushString(handlerPtr->info.path, IO_NOW, sampleString);
                }
                else
                {
                    psensor_PushString(handlerPtr->info.sensorRef, IO_NOW, sampleString);
                }
            }
            else
            {
                LE_ERROR("Error sampling sensor");
                return LE_FAULT;
            }
            break;

        case IO_DATA_TYPE_JSON:
            readStringValue = (pfJSON)(handlerPtr->callbacks.sample.jsonCb);
            length = sizeof(sampleString);
            result = readStringValue(sampleString,  &length, handlerPtr->pluginContextPtr);

            if (result == LE_OK)
            {
                if (handlerPtr->info.isReadOnce)
                {
                    io_PushJson(handlerPtr->info.path, IO_NOW, sampleString);
                }
                else
                {
                    psensor_PushJson(handlerPtr->info.sensorRef, IO_NOW, sampleString);
                }
            }
            else
            {
                LE_ERROR("Error sampling sensor");
                return LE_FAULT;
            }
            break;
        default:
            LE_ERROR("Error reading value");
            break;
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Pushes the sensor configuration to Datahub
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t PushConfig
(
    sensorHandler_t* handlerPtr                  ///< [IN] Handler to the registered sensor
)
{
    le_result_t result;

    pfString readStringValue;
    char sampleString[MAX_RES_STRING_LEN];
    size_t length = sizeof(sampleString);

    LE_INFO("Read config of %s", handlerPtr->info.name);

    readStringValue = (pfString)(handlerPtr->callbacks.configCb);
    result = readStringValue(sampleString, &length, handlerPtr->pluginContextPtr);

    char resourcePath[IO_MAX_RESOURCE_PATH_LEN + MAX_RESOURCE_NAME_LEN];
    snprintf(resourcePath, sizeof(resourcePath), "%s/%s", handlerPtr->info.path, "config");

    if (result == LE_OK)
    {
        LE_INFO("set %s to %s", resourcePath, sampleString);
        io_PushJson(resourcePath, IO_NOW, sampleString);

    }
    else
    {
        LE_ERROR("Error reading sensor configuration");
        return LE_FAULT;
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback when an update is received from the Data Hub for the "config"
 */
//--------------------------------------------------------------------------------------------------
static void ConfigUpdateHandler
(
    double timestamp,                           ///< timestamp
    const char* jsonStringPtr,                  ///< incoming JSON config
    void* contextPtr                            ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("Received update to 'config' : (timestamped %lf)"
            "value = %s", timestamp, jsonStringPtr);

    // Use the context and call appropriate callback function
    sensorHandler_t* handlerPtr = (sensorHandler_t*)contextPtr;

    if (handlerPtr == NULL)
    {
        LE_ERROR("Sensor context empty");
        return;
    }

    LE_INFO("Config %s", handlerPtr->info.name);

    size_t configSize = strlen(jsonStringPtr);
    handlerPtr->callbacks.configCb((char*)jsonStringPtr,
                                   &configSize,
                                   handlerPtr->pluginContextPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Function called by the periodicSensor component when it's time to sample
 */
//--------------------------------------------------------------------------------------------------
static void SampleSensor
(
    psensor_Ref_t sensorRef,            ///< [IN] Periodic sensor reference
    void *contextPtr                    ///< [IN] Sensor context
)
{
    // Use the context and call appropriate callback function
    sensorHandler_t* handlerPtr = (sensorHandler_t*)contextPtr;

    if (handlerPtr == NULL)
    {
        LE_ERROR("Sensor context empty");
        return;
    }

    PushData(handlerPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Creates a input/output in the datahub
 */
//--------------------------------------------------------------------------------------------------
static void AddDataHubEntry
(
    sensorHandler_t* handlerPtr                  ///< [IN] handler
)
{
    le_result_t result;

    if (handlerPtr->info.isReadOnce)
    {
        // Create a input in datahub.
        handlerPtr->info.sensorRef = NULL;

        LE_INFO("Create a resource and push data once");
        result = io_CreateInput(handlerPtr->info.path,
                                handlerPtr->info.type,
                                handlerPtr->info.unit);

        LE_ASSERT((result == LE_OK) || (result == LE_DUPLICATE));
    }
    else
    {
        // Create a periodic sensor
        LE_INFO("Creating a periodic sensor %s", handlerPtr->info.path);

        handlerPtr->info.sensorRef = psensor_Create(handlerPtr->info.path,
                                                    handlerPtr->info.type,
                                                    handlerPtr->info.unit,
                                                    SampleSensor,
                                                    (void*)handlerPtr);

        LE_ASSERT(handlerPtr->info.sensorRef != NULL);

        char resourcePath[IO_MAX_RESOURCE_PATH_LEN + MAX_RESOURCE_NAME_LEN];

        // enable periodic sensor
        snprintf(resourcePath, sizeof(resourcePath), "%s/%s", handlerPtr->info.path, "enable");
        io_PushBoolean(resourcePath, IO_NOW, true);

        // set the default period
        snprintf(resourcePath, sizeof(resourcePath), "%s/%s", handlerPtr->info.path, "period");
        io_PushNumeric(resourcePath, IO_NOW, DEFAULT_SAMPLING_PERIOD_SEC);
    }

    // sample once now
    PushData(handlerPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Parses the json document and fills up the sensor info.
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ParseSensorInfo
(
    const char* jsonStringPtr,             ///< [IN]  Json string describing the sensor
    sensorInfo_t* sensorInfoPtr            ///< [OUT] Parsed structure
)
{
    le_result_t result;
    char extractedData[128];
    json_DataType_t extractedType;

    // Read sensor name
    result = json_Extract(extractedData,
                          sizeof(extractedData),
                          jsonStringPtr,
                          "name",
                          &extractedType);

    if (result == LE_OK)
    {
        if (LE_OK != le_utf8_Copy(sensorInfoPtr->name, extractedData, sizeof(sensorInfoPtr->name), NULL))
        {
            LE_ERROR("Buffer is too small to copy json string");
            return LE_FAULT;
        }
    }
    else
    {
        LE_ERROR("Error reading sensor name");
        return LE_FAULT;
    }

    // Read sensor path
    result = json_Extract(extractedData,
                          sizeof(extractedData),
                          jsonStringPtr,
                          "path",
                          &extractedType);

    if (result == LE_OK)
    {
        if (LE_OK != le_utf8_Copy(sensorInfoPtr->path, extractedData, sizeof(sensorInfoPtr->path), NULL))
        {
            LE_ERROR("Buffer is too small to copy json string");
            return LE_FAULT;
        }
    }
    else
    {
        LE_ERROR("Error reading sensor path");
        return LE_FAULT;
    }

    // Find if sensor is read once?
    result = json_Extract(extractedData,
                          sizeof(extractedData),
                          jsonStringPtr,
                          "readOnce",
                          &extractedType);

    if (result == LE_OK)
    {
        sensorInfoPtr->isReadOnce = json_ConvertToBoolean(extractedData);
    }
    else
    {
        // isReadOnce is optional and default to false if not available.
        sensorInfoPtr->isReadOnce = false;
    }

    // Read sensor unit of measurement
    result = json_Extract(extractedData,
                          sizeof(extractedData),
                          jsonStringPtr,
                          "unit",
                          &extractedType);

    if (result != LE_OK)
    {
        LE_ERROR("Error reading sensor unit");
        return LE_FAULT;
    }

    if (LE_OK != le_utf8_Copy(sensorInfoPtr->unit, extractedData, sizeof(sensorInfoPtr->unit), NULL))
    {
        LE_ERROR("Buffer is too small to copy json string");
        return LE_FAULT;
    }

    LE_DEBUG("name = %s", sensorInfoPtr->name);
    LE_DEBUG("path = %s", sensorInfoPtr->path);
    LE_DEBUG("unit = %s", sensorInfoPtr->unit);
    LE_DEBUG("isReadOnce = %d", sensorInfoPtr->isReadOnce);

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Sample a sensor and push the data
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t sensorFw_PushSample
(
    void* handlerPtr                          ///< [OUT] Sensor handler
)
{
    if (handlerPtr == NULL)
    {
        LE_ERROR("Sensor handler is NULL");
        return LE_FAULT;
    }

    return PushData((sensorHandler_t*)handlerPtr);
}

//--------------------------------------------------------------------------------------------------
/**
 * Register a callback function to the sensor framework
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
LE_SHARED le_result_t sensorFw_RegisterCallback
(
    const char* jsonStringPtr,                ///< [IN] Standard information describing the sensor/actuator
    sensorfwDataType_t type,                  ///< [IN] Data type returned by the callback
    sensorfwCallbacks_t* cbPtr,               ///< [IN] Callbacks to operate the sensor described in info
    void* contextPtr,                         ///< [IN] Context passed by plugin
    void* returnHandler                       ///< [OUT] Sensor handler (not used)
)
{
    le_result_t result;
    LE_INFO("Register a sensor");

    sensorHandler_t* handlerPtr = le_mem_ForceAlloc(SensorHandlerPool);

    // Save sensor information provided by the plugin
    if (ParseSensorInfo(jsonStringPtr, &handlerPtr->info) != LE_OK)
    {
        LE_ERROR("Parsing sensor info failed");
        le_mem_Release(handlerPtr);
        return LE_FAULT;
    }

    // Register callback funtions
    handlerPtr->info.sensorId = RegisteredSensorCount;
    handlerPtr->callbacks.configCb = cbPtr->configCb;

    // Save plugin context
    handlerPtr->pluginContextPtr = contextPtr;

    // Add an entry to data hub.
    switch (type)
    {
        case SF_CB_NUMERIC:
            handlerPtr->info.type = IO_DATA_TYPE_NUMERIC;
            handlerPtr->callbacks.sample.numericCb = cbPtr->sample.numericCb;
            break;

        case SF_CB_BOOLEAN:
            handlerPtr->info.type = IO_DATA_TYPE_BOOLEAN;
            handlerPtr->callbacks.sample.boolCb = cbPtr->sample.boolCb;
            break;

        case SF_CB_STRING:
            handlerPtr->info.type = IO_DATA_TYPE_STRING;
            handlerPtr->callbacks.sample.stringCb = cbPtr->sample.stringCb;
            break;

        case SF_CB_JSON:
            handlerPtr->info.type = IO_DATA_TYPE_JSON;
            handlerPtr->callbacks.sample.jsonCb = cbPtr->sample.jsonCb;
            break;

        default:
            LE_ERROR("Invalid data type for callback");
            return LE_FAULT;
    }

    // Create a resource in datahub.
    AddDataHubEntry(handlerPtr);
    RegisteredSensorCount++;

    // Create a standard json config field for this sensor.
    if ((!handlerPtr->info.isReadOnce) && (cbPtr->configCb != NULL))
    {
        char resourcePath[IO_MAX_RESOURCE_PATH_LEN + MAX_RESOURCE_NAME_LEN];
        snprintf(resourcePath, sizeof(resourcePath), "%s/%s", handlerPtr->info.path, "config");

        LE_INFO("create %s", resourcePath);

        result = io_CreateOutput(resourcePath,
                                 IO_DATA_TYPE_JSON,
                                 "");

        LE_ASSERT((result == LE_OK) || (result == LE_DUPLICATE));

        // Read Initial configuration and push to datahub
        PushConfig(handlerPtr);

        // Register for notification when datahub updates the config
        io_AddJsonPushHandler(resourcePath, ConfigUpdateHandler, handlerPtr);
    }

    return LE_OK;
}

COMPONENT_INIT
{
    LE_INFO("Start sensor FW App");

    SensorHandlerPool = le_mem_InitStaticPool(SensorHandlerPool,
                                              SENSOR_HANDLER_POOL_SIZE,
                                              sizeof(sensorHandler_t));
}
