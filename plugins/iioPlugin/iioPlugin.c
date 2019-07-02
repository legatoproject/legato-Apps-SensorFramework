//--------------------------------------------------------------------------------------------------
/** @file iioSensors.c
 *
 * This component implements the callbacks to read iio sensors and registers them to the sensor
 * framework.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "iio.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "sensorFw.h"
#include "jansson.h"

//--------------------------------------------------------------------------------------------------
/**
 * Default unit of measurement for sensor value.
 */
//--------------------------------------------------------------------------------------------------
#define     DEFAULT_MEAS_UNIT               ""

//--------------------------------------------------------------------------------------------------
/**
 * Periodic sensor pool size
 */
//--------------------------------------------------------------------------------------------------
#define     SENSOR_CONTEXT_POOL_SIZE        100

//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes of the read value when represented as a string
 */
//--------------------------------------------------------------------------------------------------
#define     MAX_ATTR_LENGTH                 128


//--------------------------------------------------------------------------------------------------
/**
 * Maximum number of bytes of the JSON string describing the sensor
 */
//--------------------------------------------------------------------------------------------------
#define     MAX_JSON_SIZE                   1024


//--------------------------------------------------------------------------------------------------
/**
 * Precision up to 6 decimal place
 */
//--------------------------------------------------------------------------------------------------
#define     REAL_PRECISION                  JSON_REAL_PRECISION(6)


//--------------------------------------------------------------------------------------------------
/**
 * Structure that defines the standard unit of measurement for different IIO sensors.
 *
 * Refer IIO documentation: https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-iio
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    const char *measurementName;         ///< Sensor name as defined in iio
    const char *standardUnit;            ///< Unit of measurement as defined in iio
}
iioUnits_t;


//--------------------------------------------------------------------------------------------------
/**
 * Context of the iio sensor
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    struct iio_device* device;
    const struct iio_channel* chan;
}
iioSensorContext_t;


//--------------------------------------------------------------------------------------------------
/**
 * Error type
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    ATTRIBUTE_FOUND,
    ATTRIBUTE_NOT_FOUND,
    ATTRIBUTE_FAULT
}
attrErrorType_t;


//--------------------------------------------------------------------------------------------------
/**
 * List of sensors and their standard unit of measurement.
 */
//--------------------------------------------------------------------------------------------------
iioUnits_t IioStandardUnit[] =
{
    // Sensor Name                  // Unit of measurement
    { "temp",                       "milli degree celcius"                  },
    { "pressure",                   "kilo pascals"                          },
    { "anglvel",                    "radians per second"                    },
    { "voltage",                    "millivolts"                            },
    { "current",                    "milliamps"                             },
    { "power",                      "milliwatts"                            },
    { "capacitance",                "nanofarads"                            },
    { "positionrelative",           "milli percent"                         },
    { "magn",                       "Gauss"                                 },
    { "accel",                      "m/s^2"                                 },
    { "incli",                      "degrees"                               },
    { "humidity",                   "milli percent"                         },
    { "proximity",                  "meters"                                }
};

//--------------------------------------------------------------------------------------------------
/**
 * Get JSON document describing the sensor/actuator.
 *
 *  * @return:
 *      - LE_OK if attribute is available
 *      - LE_OVERFLOW if attribute is NOT available
 *      - ATTRIBUTE_FAULT if there is an error when reading the attribute
 */
//--------------------------------------------------------------------------------------------------
static le_result_t CreateJsonDocument
(
    char* buffer,                       ///< [IN] Buffer Pool
    size_t bufferSize,                  ///< [IN] Buffer size
    const char* name,                   ///< [IN] Sensor name
    const char* path,                   ///< [IN] Relative path of sensor in dataHub
    bool isReadOnce,                    ///< [IN] Is sensor read once only?
    const char* unit                    ///< [IN] Measurement unit
)
{
    char* isReadOnceString = isReadOnce? "true" : "false";

    int res = snprintf(buffer,
                       bufferSize,
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

    if (res < 0)
    {
        return LE_FAULT;
    }
    else if (res >= bufferSize)
    {
        return LE_OVERFLOW;
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Get the value of an attribute using the attribute name.
 *
 * @return:
 *      - ATTRIBUTE_FOUND if attribute is available
 *      - ATTRIBUTE_NOT_FOUND if attribute is NOT available
 *      - ATTRIBUTE_FAULT if there is an error when reading the attribute
 */
//--------------------------------------------------------------------------------------------------
static attrErrorType_t GetAttribute
(
    const struct iio_channel* chan,             ///< [IN] IIO Channel
    const char* attrName,                       ///< [IN] Attribute name
    double* readValuePtr                        ///< [OUT] Value
)
{
    char attrVal[MAX_ATTR_LENGTH];
    size_t readLen;

    if (iio_channel_find_attr(chan, attrName))
    {
        readLen = iio_channel_attr_read(chan, attrName, attrVal, sizeof(attrVal));
        if (readLen > 0)
        {
            *readValuePtr = atof(attrVal);
        }
        else
        {
            return ATTRIBUTE_FAULT;
        }
    }
    else
    {
        return ATTRIBUTE_NOT_FOUND;
    }

    return ATTRIBUTE_FOUND;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the unit of measurement
 *
 * @return:
 *         Unit of measurement if name is identified, else returns "".
 */
//--------------------------------------------------------------------------------------------------
static const char* GetIiounit
(
    char* measNamePtr                        ///< The thing we are measuring
)
{
    int i;

    for (i = 0; i < (sizeof(IioStandardUnit) / sizeof((IioStandardUnit)[0])); i++)
    {
        if (strstr(measNamePtr, IioStandardUnit[i].measurementName) != NULL)
        {
            return (char*)IioStandardUnit[i].standardUnit;
        }
    }

    return (char*)DEFAULT_MEAS_UNIT;
}


//--------------------------------------------------------------------------------------------------
/**
 * Write the IIO attribute
 */
//--------------------------------------------------------------------------------------------------
static void UpdateIioConfig
(
    json_t* incomingJsonData,
    char* attrPtr,
    const struct iio_channel* chan
)
{
    char writeVal[MAX_ATTR_LENGTH];
    char readVal[MAX_ATTR_LENGTH];

    if (json_is_number(incomingJsonData))
    {
        double configValue = json_number_value(incomingJsonData);
        snprintf(writeVal, sizeof(writeVal), "%f", configValue);

        iio_channel_attr_read(chan, attrPtr, readVal, sizeof(readVal));

        if (strncmp(readVal, writeVal, sizeof(readVal)) != 0)
        {
            LE_INFO("Update %s value from %s to %s", attrPtr, readVal, writeVal);

            // ToDo: updating attributes result in crash.
            // iio_channel_attr_write(chan, attrPtr, writeVal);
        }
    }
    else
    {
        LE_ERROR("JSON config data is not a number");
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Process incoming JSON string (Config write operation from server)
 */
//--------------------------------------------------------------------------------------------------
static void ProcessIncomingConfig
(
    const char* jsonStringPtr,
    char* attrPtr,
    const struct iio_channel* chan
)
{
    int i;

    // load incoming json string and update the value
    if (jsonStringPtr != NULL)
    {
        json_t* jsonRootPtr = json_loads(jsonStringPtr, 0, NULL);

        if (jsonRootPtr != NULL)
        {
            json_t* incomingAttr = json_object_get(jsonRootPtr, attrPtr);

            if (incomingAttr == NULL)
            {
                return;
            }

            if(!json_is_array(incomingAttr))
            {
                UpdateIioConfig(incomingAttr, attrPtr, chan);
            }
            else
            {
                for(i = 0; i < json_array_size(incomingAttr); i++)
                {
                    json_t* incomingJsonData = json_array_get(incomingAttr, i);
                    UpdateIioConfig(incomingJsonData, attrPtr, chan);
                }
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Find the attribute and add it to the JSON object
 *
 * @return:
 *         - LE_OK if attribute found
 *         - LE_NOT_FOUND if attribute not found
 */
//--------------------------------------------------------------------------------------------------
static le_result_t AddAttrToJson
(
    const struct iio_channel* chan,
    json_t* jsonObj,
    char* attrPtr,
    const char* incomingConfigPtr
)
{
    char attrVal[MAX_JSON_SIZE];
    char* tokenPtr;
    char* savePtr;
    json_t* arr;
    double readValue;

    if(iio_channel_find_attr(chan, attrPtr))
    {
        iio_channel_attr_read(chan, attrPtr, attrVal, sizeof(attrVal));

        tokenPtr = strtok_r(attrVal , " ", &savePtr);

        if (tokenPtr == NULL)
        {
            readValue = strtod(attrVal, NULL);

            json_object_set_new(jsonObj, attrPtr, json_real(readValue));

            // Process incoming configuration data.
            ProcessIncomingConfig(incomingConfigPtr, attrPtr, chan);
        }
        else
        {
            arr = json_array();

            while(tokenPtr)
            {
                readValue = strtod(tokenPtr, NULL);
                json_array_append_new(arr, json_real(readValue));
                tokenPtr = strtok_r(NULL , " ", &savePtr);

                // Process incoming configuration data.
                ProcessIncomingConfig(incomingConfigPtr, attrPtr, chan);
            }

            // Add array to json object
            json_object_set_new(jsonObj, attrPtr, arr);
        }
    }
    else
    {
        return LE_NOT_FOUND;
    }

    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Read/Write iio configuration in JSON format
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t ConfigIioSensor
(
    char* jsonStringPtr,                                  ///< [INOUT] JSON configuration
    size_t* lengthPtr,                                    ///< [INOUT] length
    void *contextPtr                                      ///< [IN] Context of the sensor
)
//--------------------------------------------------------------------------------------------------
{
    const char* deviceName;
    iioSensorContext_t* sensorCtxtPtr = (iioSensorContext_t*)(contextPtr);

    json_t* sensorConfigObj = json_object();

    if (jsonStringPtr == NULL)
    {
        LE_ERROR("Buffer pointer is NULL");
        return LE_FAULT;
    }

    if (sensorCtxtPtr == NULL)
    {
        LE_ERROR("Sensor context empty");
        return LE_FAULT;
    }
    else
    {
        deviceName = iio_device_get_name(sensorCtxtPtr->device);
    }

    if ((deviceName == NULL) || (sensorCtxtPtr->chan == NULL))
    {
        LE_ERROR("Device name or channel name is empty");
        return LE_FAULT;
    }

    const char* channelName = (char*)iio_channel_get_id(sensorCtxtPtr->chan);
    LE_INFO("Config '%s/%s'", deviceName, channelName);

    // Add sampling frequency
    if (iio_channel_find_attr(sensorCtxtPtr->chan, "sampling_frequency_available"))
    {
        LE_INFO("Adjust sampling frequency");
        AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "sampling_frequency", jsonStringPtr);
    }
    else
    {
        AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "sampling_frequency", NULL);
    }

    // Add scale
    if (iio_channel_find_attr(sensorCtxtPtr->chan, "scale_available"))
    {
        LE_INFO("Adjust scale");
        AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "scale", jsonStringPtr);
    }
    else
    {
        AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "scale", NULL);
    }

    // Add scale available (Read only)
    AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "scale_available", NULL);

    // Add sampling frequency available (Read only)
    AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "sampling_frequency_available", NULL);

    // Add oversampling ratio available (Read only)
    AddAttrToJson(sensorCtxtPtr->chan, sensorConfigObj, "oversampling_ratio_available", NULL);

    // convert to a json string
    char* str = json_dumps(sensorConfigObj, REAL_PRECISION);
    json_decref(sensorConfigObj);

    le_utf8_Copy(jsonStringPtr, str, *lengthPtr, NULL);
    free(str);
    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Sample iio sensor
 *
 * @return:
 *      - LE_OK on success
 *      - LE_FAULT on any other error
 */
//--------------------------------------------------------------------------------------------------
static le_result_t SampleIioSensor
(
    double* readValuePtr,                               ///< [OUT] Sampled value
    size_t* lengthPtr,                                  ///< [INOUT] length
    void *contextPtr                                    ///< [IN] Context of the sensor
)
//--------------------------------------------------------------------------------------------------
{
    double inputValue;
    const char* deviceName;

    iioSensorContext_t* sensorCtxtPtr = (iioSensorContext_t*)(contextPtr);

    if (sensorCtxtPtr == NULL)
    {
        LE_ERROR("Sensor context empty");
        return LE_FAULT;
    }
    else
    {
        deviceName = iio_device_get_name(sensorCtxtPtr->device);
    }

    if ((deviceName == NULL) || (sensorCtxtPtr->chan == NULL))
    {
        LE_ERROR("Device name or channel name is empty");
        return LE_FAULT;
    }

    const char* channelName = (char*)iio_channel_get_id(sensorCtxtPtr->chan);

    attrErrorType_t result = GetAttribute(sensorCtxtPtr->chan, "input", &inputValue);

    // Sensor can be sampled only if "input" or "raw" value is available.
    if(result == ATTRIBUTE_FOUND)
    {
        *readValuePtr = inputValue;
    }
    else
    {
        // If value is not scaled already, read the raw value and
        // scale it accordingly.
        // ToDo: Test this with other sensor types. What about
        // calibration data?
        double raw, offset, scale, finalValue;

        // Check if raw value is available.
        GetAttribute(sensorCtxtPtr->chan, "raw", &raw);
        GetAttribute(sensorCtxtPtr->chan, "offset", &offset);
        GetAttribute(sensorCtxtPtr->chan, "scale", &scale);

        finalValue = (raw * scale) + (offset * scale);

        *readValuePtr = finalValue;
    }

    LE_INFO("Sample value of '%s/%s' is %lf", deviceName, channelName, *readValuePtr);
    return LE_OK;
}


//--------------------------------------------------------------------------------------------------
/**
 * Init IIO plugin
 */
//--------------------------------------------------------------------------------------------------
static void IioPluginInit
(
    void
)
{
    unsigned int i;
    char resourcePath[256];
    struct iio_device *device;
    char* channelName;
    unsigned int j;
    const struct iio_channel *chan;
    const char* deviceName;
    double inputValue;
    char jsonDoc[MAX_JSON_SIZE];
    le_result_t res;

    struct iio_context *localCtx = iio_create_local_context();

    if (localCtx == NULL)
    {
        LE_ERROR("Failed to create iio local context");
        return;
    }

    if (iio_context_set_timeout(localCtx, 5000) != 0)
    {
        LE_ERROR("Failed to set timeout");
        return;
    }

    // Initialize the callbacks
    sensorfwCallbacks_t pluginCb;

    pluginCb.configCb = ConfigIioSensor;
    pluginCb.sample.numericCb = SampleIioSensor;

    for (i = 0, device = iio_context_get_device(localCtx, i);
         device != NULL;
         ++i, device = iio_context_get_device(localCtx, i))
    {

        deviceName = iio_device_get_name(device);

        // ToDo: Document unit of measurement.
        for (j = 0, chan = iio_device_get_channel(device, j);
             chan != NULL;
             ++j, chan = iio_device_get_channel(device, j))
        {
            channelName = (char*)iio_channel_get_id(chan);
            snprintf(resourcePath, sizeof(resourcePath), "%s/%s", deviceName, channelName);

            // Create a resource to read sensor sample
            if (iio_channel_is_output(chan))
            {
                snprintf(resourcePath, sizeof(resourcePath), "%s/%s", resourcePath, "value");
                LE_ERROR("Registering output - TO BE IMPLEMENTED");
            }
            else
            {
                attrErrorType_t attrErr = GetAttribute(chan, "input", &inputValue);

                // Set context that will be passed to periodic sample function.
                iioSensorContext_t* sensorCtxtPtr = malloc(sizeof(iioSensorContext_t));
                memset(sensorCtxtPtr, 0, sizeof(iioSensorContext_t));

                sensorCtxtPtr->device = device;
                sensorCtxtPtr->chan = chan;

                // create numeric input named "value" and push sample data.
                if(attrErr == ATTRIBUTE_FOUND)
                {
                    // Register the sensor
                    LE_INFO("Register the sensor %s", resourcePath);

                    // Format information relating to sensor in JSON format
                    res = CreateJsonDocument((char*)&jsonDoc,
                                             sizeof(jsonDoc),
                                             (const char*)&resourcePath,
                                             (const char*)&resourcePath,
                                             false,
                                             GetIiounit(channelName));

                    if (res != LE_OK)
                    {
                        LE_ERROR("Error registering callback");
                        return;
                    }

                    attrErr = sensorFw_RegisterCallback((char*)&jsonDoc,
                                                       SF_CB_NUMERIC,
                                                       &pluginCb,
                                                       (void*)sensorCtxtPtr,
                                                       NULL);
               }
                else if (attrErr == ATTRIBUTE_NOT_FOUND)
                {
                    double raw;

                    // Check if raw value is available.
                    attrErr = GetAttribute(chan, "raw", &raw);

                    if (attrErr == ATTRIBUTE_FOUND)
                    {
                        LE_INFO("Register the sensor %s", resourcePath);

                        // Format information relating to sensor in JSON format
                        res = CreateJsonDocument((char*)&jsonDoc,
                                                 sizeof(jsonDoc),
                                                 (const char*)&resourcePath,
                                                 (const char*)&resourcePath,
                                                 false,
                                                 GetIiounit(channelName));

                        if (res != LE_OK)
                        {
                            LE_ERROR("Error registering callback");
                            return;
                        }

                        attrErr = sensorFw_RegisterCallback((char*)&jsonDoc,
                                                           SF_CB_NUMERIC,
                                                           &pluginCb,
                                                           (void*)sensorCtxtPtr,
                                                           NULL);
                    }
                    else
                    {
                        LE_ERROR("Error reading raw value of sensor");
                    }
                }
                else
                {
                    LE_ERROR("Error reading input value of sensor");
                }
            }
        }
    }
}


COMPONENT_INIT
{
    LE_INFO("Start iio plugin");

    IioPluginInit();
}
