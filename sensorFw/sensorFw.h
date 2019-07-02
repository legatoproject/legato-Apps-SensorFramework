//--------------------------------------------------------------------------------------------------
/** @file sensorFw.h
 *
 * Defines callbacks that need to be implemented by the plugins and provides apis to register those
 * callback functions.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#ifndef LEGATO_SENSOR_FW_COMP_INCLUDE_GUARD
#define LEGATO_SENSOR_FW_COMP_INCLUDE_GUARD

//--------------------------------------------------------------------------------------------------
/**
 * Data type returned by callbacks
 */
//--------------------------------------------------------------------------------------------------
typedef enum
{
    SF_CB_NUMERIC,
    SF_CB_STRING,
    SF_CB_BOOLEAN,
    SF_CB_JSON
}
sensorfwDataType_t;

//--------------------------------------------------------------------------------------------------
/**
 * Types of callback functions provided by the plugin
 */
//--------------------------------------------------------------------------------------------------
typedef le_result_t (*pfBool)   (bool* readBoolValue, size_t* lengthPtr, void* contextPtr);
typedef le_result_t (*pfNumeric)(double* readNumericValue, size_t* lengthPtr, void* contextPtr);
typedef le_result_t (*pfString) (char* readStringValue, size_t* lengthPtr, void* contextPtr);
typedef le_result_t (*pfJSON)   (char* readJsonValue, size_t* lengthPtr, void* contextPtr);

//--------------------------------------------------------------------------------------------------
/**
 * Callbacks to operate the sensor
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    pfString    infoCb;    // Information about the name, units, callback and types etc.
    pfString    configCb;  // Per device configuration in JSON, passed through to plugin/datahub
    union
    {
        pfBool      boolCb;    // Read or write a boolean value
        pfNumeric   numericCb; // Read or write a numeric value
        pfString    stringCb;  // Read or write a string value
        pfJSON      jsonCb;    // Read or write a JSON structure
    }sample;
}
sensorfwCallbacks_t;

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
    const char* sensorInfoPtr,                ///< [IN] JSON document describing the sensor/actuator
    sensorfwDataType_t type,                  ///< [IN] Data type returned by the callback
    sensorfwCallbacks_t* callbackPtr,         ///< [IN] Callbacks to operate the sensor described in info
    void* contextPtr,                         ///< [IN] Context passed by plugin
    void* handlerPtr                          ///< [OUT] Sensor handler
);

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
);

#endif /* LEGATO_SENSOR_FW_COMP_INCLUDE_GUARD */
